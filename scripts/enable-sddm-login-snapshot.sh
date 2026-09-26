#!/bin/sh
set -eu

helper=/usr/lib/libexec/plasmaglow-login-snapshot
test "$(id -u)" -eq 0
test -x "$helper"

for service in sddm sddm-autologin; do
    pam_file="/etc/pam.d/$service"
    test -f "$pam_file"
    cp -n "$pam_file" "$pam_file.plasmaglow-backup"
    for operation in open_session close_session; do
        line="session optional pam_exec.so type=$operation $helper"
        if ! grep -Fqx "$line" "$pam_file"; then
            printf '%s\n' "$line" >> "$pam_file"
        fi
    done
done
