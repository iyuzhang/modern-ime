#!/usr/bin/env bash
set -euo pipefail

prefix="$HOME/.local"
backup_dir="${XDG_STATE_HOME:-$HOME/.local/state}/modern-ime/install-backup"
profile="$HOME/.config/fcitx5/profile"
systemctl --user disable --now modern-ime-ui.service modern-ime-service.service || true
rm -f "$HOME/.config/systemd/user/modern-ime-ui.service" "$HOME/.config/systemd/user/modern-ime-service.service"
rm -f "$HOME/.config/environment.d/90-modern-ime.conf"
rm -f "$prefix/bin/modern-ime-service" "$prefix/bin/modern-ime-voice-worker" "$prefix/bin/modern-ime-candidate-ui" "$prefix/bin/modern-ime-settings"
rm -f "$prefix/lib/fcitx5/libmodernime.so" "$prefix/lib/fcitx5/libmodernimeui.so"
fcitx_library_dir="$(pkg-config --variable=libdir Fcitx5Core)/fcitx5"
sudo -n rm -f "$fcitx_library_dir/libmodernime.so" "$fcitx_library_dir/libmodernimeui.so"
rm -f "$prefix/share/fcitx5/addon/modernime.conf" "$prefix/share/fcitx5/addon/modernimeui.conf" "$prefix/share/fcitx5/inputmethod/modernime.conf" "$prefix/share/applications/modern-ime-settings.desktop"
rm -f "$prefix/share/dbus-1/services/org.modernime.Service1.service" "$prefix/share/dbus-1/services/org.modernime.UI1.service"
rm -f "$prefix/share/modern-ime/install-voice-model.sh" "$prefix/share/modern-ime/dbus/org.modernime.Service1.xml"
rm -f "$prefix/share/doc/modern-ime/README.md" "$prefix/share/doc/modern-ime/copyright"
rm -f "$prefix/lib/systemd/user/modern-ime-service.service" "$prefix/lib/systemd/user/modern-ime-ui.service" "$prefix/lib/systemd/user/modern-ime-voice-worker.service"
if [[ -f "$backup_dir/profile.before-modern-ime" ]]; then cp --preserve=mode,timestamps "$backup_dir/profile.before-modern-ime" "$profile"; fi
systemctl --user daemon-reload
fcitx5-remote -r || true
printf 'Modern IME programs removed. Personal data under XDG data was preserved.\n'
