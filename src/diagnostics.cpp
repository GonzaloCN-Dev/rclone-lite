#include "diagnostics.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

#include "store.h"

namespace rl {
namespace {

std::string low(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool has_any(const std::string &l, std::initializer_list<const char *> needles)
{
    for (const char *n : needles)
        if (l.find(n) != std::string::npos)
            return true;
    return false;
}

} // namespace

ErrorHint explain_error(const std::string &raw)
{
    const std::string l = low(raw);
    if (l.empty())
        return {};
    if (l.find(low(transfer_busy_message())) != std::string::npos || l.find("transferencia de rclone lite") != std::string::npos)
        return { "busy", "Hay otra transferencia en curso",
                 "Espera a que termine; las operaciones iniciadas desde la app esperan su turno automáticamente." };
    if (is_auth_error(raw))
        return { "auth", "El acceso a la cuenta ha caducado o fue revocado",
                 "Vuelve a iniciar sesión desde Remotos con Reconectar." };
    if (has_any(l, { "quota exceeded", "ratelimitexceeded", "rate limit exceeded", "storagequotaexceeded", "too many requests",
                     "userratelimitexceeded", "download quota", "error 429", "status code 429" }))
        return { "quota", "El servicio ha agotado la cuota o limita las peticiones",
                 "Espera unas horas o reduce transferencias simultáneas. En Google Drive, usa credenciales propias en Remotos." };
    if (has_any(l, { "no space left", "not enough space", "disk full", "insufficient storage", "storage full",
                     "there is not enough space on the disk", "espacio en disco" }))
        return { "disk_full", "No hay espacio suficiente",
                 "Libera espacio en el destino (o amplía el plan de la cuenta) y reintenta." };
    if (has_any(l, { "permission denied", "access is denied", "access denied", "forbidden", "insufficientpermissions",
                     "insufficient permission", "error 403", "status code 403", "acceso denegado", "operation not permitted" }))
        return { "permission", "No hay permisos para esta operación",
                 "Comprueba que tu cuenta puede escribir en el destino y que ningún programa usa los archivos." };
    if (has_any(l, { "directory not found", "couldn't find", "could not find", "no such file", "object not found",
                     "file not found", "cannot find the path", "cannot find the file", "error listing: directory not found",
                     "didn't find section in config file", "error 404", "status code 404" }))
        return { "not_found", "No se encuentra la ruta o el remoto",
                 "Revisa que el origen y el destino existan y que el remoto siga configurado." };
    if (has_any(l, { "no such host", "connection refused", "i/o timeout", "dial tcp", "network is unreachable",
                     "connection reset", "tls handshake timeout", "context deadline exceeded", "temporary failure in name resolution" }))
        return { "network", "No se pudo conectar con el servicio",
                 "Comprueba tu conexión a internet y reintenta." };
    return {};
}

std::string friendly_error(const std::string &raw)
{
    ErrorHint h = explain_error(raw);
    if (!h.known())
        return raw;
    return h.title + ". " + h.action;
}

BackgroundTask parse_task_info(const std::string &text)
{
    BackgroundTask t;
    t.queried = true;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? text.size() : end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        char *tail = nullptr;
        const long long v = std::strtoll(value.c_str(), &tail, 10);
        const bool numeric = !value.empty() && tail != value.c_str() && *tail == '\0';
        if (key == "exists")
            t.exists = numeric && v == 1;
        else if (key == "last" && numeric)
            t.last_run = v;
        else if (key == "next" && numeric)
            t.next_run = v;
        else if (key == "result" && numeric) {
            t.last_result = v;
            t.last_result_known = true;
        }
    }
    return t;
}

BackgroundSummary summarize_background(bool enabled, bool has_schedules, const BackgroundTask &task,
                                       const BackgroundRun &run)
{
    BackgroundSummary s;
    if (!enabled) {
        s.text = "Desactivado: los trabajos programados solo se ejecutan con la app abierta.";
        return s;
    }
    if (!has_schedules) {
        s.text = "Activado, pero ningún trabajo tiene programación.";
        return s;
    }
    std::string text;
    if (task.queried && !task.exists) {
        s.problem = true;
        text = "La tarea de Windows no está registrada. Guarda los ajustes para volver a crearla.";
    } else if (!task.queried) {
        text = "Comprobando la tarea de Windows…";
    } else {
        text = task.last_run <= 0 ? "La tarea de Windows aún no se ha ejecutado."
                                  : "Tarea de Windows: última ejecución " + format_time(task.last_run);
        // 0x41301 means the task is running right now; 0x41303 means it has not run yet.
        if (task.last_result_known && task.last_result != 0 && task.last_result != 267009 && task.last_result != 267011) {
            s.problem = true;
            text += " (Windows devolvió el código " + std::to_string(task.last_result) + ")";
        }
        if (task.next_run > 0)
            text += ". Próxima comprobación: " + format_time(task.next_run);
    }
    if (run.time > 0) {
        text += "\nÚltimos trabajos: " + format_time(run.time) + ", " + std::to_string(run.jobs) +
                (run.jobs == 1 ? " trabajo" : " trabajos");
        if (run.failures > 0) {
            s.problem = true;
            text += ", " + std::to_string(run.failures) + " con fallos";
            if (!run.last_error.empty())
                text += ": " + friendly_error(run.last_error);
        } else {
            text += ", sin fallos";
        }
    } else if (task.exists) {
        text += "\nTodavía no se ha ejecutado ningún trabajo en segundo plano.";
    }
    s.text = text;
    return s;
}

} // namespace rl

