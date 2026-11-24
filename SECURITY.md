# Security Policy

## Reporting A Vulnerability

Don't report security vulnerabilities in public issues. Use GitHub's private vulnerability reporting for this repository. If private reporting isn't enabled yet, contact the maintainer through GitHub and ask for a private reporting channel.

Don't post rclone configuration files, access tokens, passwords, private share links, or personal data in an issue or discussion. Redact paths and account details from logs and screenshots.

## Credentials And Data

Rclone Lite delegates remote credentials and provider configuration to rclone. Keep `%APPDATA%\rclone\rclone.conf` and exported job files private. Review extra rclone flags and job destinations before running destructive sync or delete operations.
