#pragma once

#include <string>
#include <utility>
#include <vector>

namespace rl {

struct ProviderField {
    std::string label;
    std::string key;
    std::string hint;
    bool secret = false;
};

struct Provider {
    std::string label;
    std::string type;
    std::string help;
    std::vector<ProviderField> fields;
    std::vector<std::pair<std::string, std::string>> fixed;
};

inline const std::vector<Provider> &providers()
{
    static const std::vector<Provider> list = {
        { "Google Drive", "drive",
          "Se abrirá el navegador para que autorices el acceso a tu cuenta de Google.", {},
          { { "scope", "drive" } } },
        { "OneDrive", "onedrive",
          "Se abrirá el navegador para iniciar sesión con tu cuenta de Microsoft.", {}, {} },
        { "Dropbox", "dropbox", "Se abrirá el navegador para autorizar el acceso a Dropbox.", {}, {} },
        { "Box", "box", "Se abrirá el navegador para autorizar el acceso a Box.", {}, {} },
        { "pCloud", "pcloud", "Se abrirá el navegador para autorizar el acceso a pCloud.", {}, {} },
        { "Amazon S3 / compatible", "s3",
          "Sirve para AWS S3, Cloudflare R2, Wasabi, MinIO, DigitalOcean Spaces y similares.",
          { { "Proveedor", "provider", "AWS, Cloudflare, Wasabi, Minio, DigitalOcean, Other", false },
            { "Access Key ID", "access_key_id", "", false },
            { "Secret Access Key", "secret_access_key", "", true },
            { "Región (opcional)", "region", "us-east-1", false },
            { "Endpoint (opcional)", "endpoint", "https://...", false } },
          {} },
        { "Backblaze B2", "b2", "Usa el Key ID y la Application Key de tu panel de Backblaze.",
          { { "Account / Key ID", "account", "", false },
            { "Application Key", "key", "", true } },
          {} },
        { "SFTP (SSH)", "sftp", "Conexión a un servidor por SSH.",
          { { "Servidor", "host", "ejemplo.com", false },
            { "Usuario", "user", "", false },
            { "Puerto (opcional)", "port", "22", false },
            { "Contraseña", "pass", "", true } },
          {} },
        { "FTP", "ftp", "Conexión a un servidor FTP.",
          { { "Servidor", "host", "ftp.ejemplo.com", false },
            { "Usuario", "user", "", false },
            { "Puerto (opcional)", "port", "21", false },
            { "Contraseña", "pass", "", true } },
          {} },
        { "WebDAV (Nextcloud, ownCloud...)", "webdav", "Para Nextcloud, ownCloud o cualquier servidor WebDAV.",
          { { "URL", "url", "https://nube.ejemplo.com/remote.php/dav/files/usuario", false },
            { "Tipo", "vendor", "nextcloud, owncloud, other", false },
            { "Usuario", "user", "", false },
            { "Contraseña", "pass", "", true } },
          {} },
        { "MEGA", "mega", "Cuenta de MEGA.",
          { { "Correo", "user", "", false },
            { "Contraseña", "pass", "", true } },
          {} },
        { "Azure Blob Storage", "azureblob", "Cuenta de almacenamiento de Azure.",
          { { "Cuenta", "account", "", false },
            { "Clave", "key", "", true } },
          {} },
        { "Cifrado (crypt)", "crypt",
          "Cifra los archivos antes de subirlos a otro remoto. Guarda bien las contraseñas: sin ellas no se pueden recuperar los datos.",
          { { "Remoto de destino", "remote", "mi-drive:carpeta-cifrada", false },
            { "Contraseña", "password", "", true },
            { "Contraseña 2 / sal (opcional)", "password2", "", true } },
          {} },
        { "HTTP (solo lectura)", "http", "Navega un sitio web con listado de archivos.",
          { { "URL", "url", "https://ejemplo.com/archivos/", false } }, {} },
        { "Otro tipo (avanzado)", "", "Indica el tipo de rclone y, si quieres, parámetros clave=valor.",
          { { "Tipo de rclone", "__type", "koofr, mailru, yandex, ...", false },
            { "Parámetros (opcional)", "__params", "clave=valor clave2=valor2", false } },
          {} },
    };
    return list;
}

} // namespace rl
