#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-"$project_root/build-release"}
prefix="$HOME/.local"
profile="$HOME/.config/fcitx5/profile"
backup_dir="${XDG_STATE_HOME:-$HOME/.local/state}/modern-ime/install-backup"
ime_xdg_data_dirs="$HOME/.local/share:/usr/share/plasma:/usr/share/gnome:$HOME/.local/share/flatpak/exports/share:/var/lib/flatpak/exports/share:/usr/local/share:/usr/share:/var/lib/snapd/desktop"

cmake -S "$project_root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DMODERN_IME_WARNINGS_AS_ERRORS=ON
cmake --build "$build_dir" -j "$(nproc)"
ctest --test-dir "$build_dir" --output-on-failure
fcitx_library_dir="$(pkg-config --variable=libdir Fcitx5Core)/fcitx5"
if ! sudo -n true; then
  printf 'Modern IME needs non-interactive sudo access to install Fcitx ABI plugins in %s.\n' "$fcitx_library_dir" >&2
  exit 1
fi

deploy_stamp=$(date +%Y%m%d-%H%M%S)
deploy_backup="$backup_dir/deploy-$deploy_stamp"
mkdir -p "$backup_dir" "$deploy_backup" "$HOME/.config/systemd/user" "$HOME/.config/environment.d"
backup_deployed_file() {
  local source=$1
  local name=$2
  if [[ -f "$source" ]]; then cp -a -- "$source" "$deploy_backup/$name"; fi
}
backup_deployed_file "$prefix/bin/modern-ime-service" user-modern-ime-service
backup_deployed_file "$prefix/bin/modern-ime-voice-worker" user-modern-ime-voice-worker
backup_deployed_file "$prefix/bin/modern-ime-candidate-ui" user-modern-ime-candidate-ui
backup_deployed_file "$prefix/bin/modern-ime-settings" user-modern-ime-settings
backup_deployed_file "$prefix/share/applications/modern-ime-settings.desktop" user-modern-ime-settings.desktop
backup_deployed_file "$fcitx_library_dir/libmodernime.so" system-libmodernime.so
backup_deployed_file "$fcitx_library_dir/libmodernimeui.so" system-libmodernimeui.so

cmake --install "$build_dir" --prefix "$prefix"
# Desktop launchers do not inherit an interactive shell's PATH. KDE therefore
# cannot resolve a bare executable installed in ~/.local/bin. Write the exact
# per-user path after CMake installs the portable desktop template.
settings_desktop="$prefix/share/applications/modern-ime-settings.desktop"
if [[ -f "$settings_desktop" ]]; then
  sed -i "s|^Exec=.*$|Exec=\"$prefix/bin/modern-ime-settings\"|" "$settings_desktop"
  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$prefix/share/applications"
  fi
fi
sudo -n install -m 0644 "$build_dir/libmodernime.so" "$fcitx_library_dir/libmodernime.so"
sudo -n install -m 0644 "$build_dir/libmodernimeui.so" "$fcitx_library_dir/libmodernimeui.so"
if [[ ${MODERN_IME_SKIP_MODEL:-0} != 1 ]]; then
  "$project_root/tools/install-voice-model.sh"
fi

printf '%s\n' '# Allow the per-user Fcitx5 addon and input-method metadata installed by Modern IME.' "XDG_DATA_DIRS=$ime_xdg_data_dirs" > "$HOME/.config/environment.d/90-modern-ime.conf"
if [[ -f "$profile" ]] && ! rg -q '^Name=modernime$' "$profile"; then
  cp --preserve=mode,timestamps "$profile" "$backup_dir/profile.before-modern-ime"
  sed -i 's/^DefaultIM=.*/DefaultIM=modernime/' "$profile"
  next_index=$(awk '/^\[Groups\/0\/Items\/[0-9]+\]$/ { item=$0; sub(/^.*Items\//, "", item); sub(/\]$/, "", item); if (item + 0 > max) max=item + 0 } END { print max + 1 }' "$profile")
  printf '\n[Groups/0/Items/%s]\nName=modernime\nLayout=\n' "$next_index" >> "$profile"
fi
install -m 0644 "$project_root/data/systemd/modern-ime-service.service" "$HOME/.config/systemd/user/modern-ime-service.service"
install -m 0644 "$project_root/data/systemd/modern-ime-ui.service" "$HOME/.config/systemd/user/modern-ime-ui.service"
systemctl --user daemon-reload
session_environment=(XDG_DATA_DIRS)
for variable in DISPLAY WAYLAND_DISPLAY XAUTHORITY XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP XDG_SESSION_TYPE; do
  if [[ -n ${!variable:-} ]]; then session_environment+=("$variable"); fi
done
XDG_DATA_DIRS="$ime_xdg_data_dirs" systemctl --user import-environment "${session_environment[@]}"
XDG_DATA_DIRS="$ime_xdg_data_dirs" dbus-update-activation-environment --systemd "${session_environment[@]}"
# Reenable removes the pre-0.1 default.target symlink when upgrading and
# installs the candidate UI under graphical-session.target instead.
systemctl --user reenable modern-ime-service.service modern-ime-ui.service
# A pre-0.1 candidate window may have been restored directly by Plasma and is
# therefore outside the systemd service cgroup. Stop only the process that
# currently owns our well-known D-Bus name and whose executable is our
# installed candidate UI; never use a broad process-name match here.
existing_ui_pid=$(gdbus call --session --dest org.freedesktop.DBus \
  --object-path /org/freedesktop/DBus \
  --method org.freedesktop.DBus.GetConnectionUnixProcessID org.modernime.UI1 \
  2>/dev/null | sed -n 's/.*uint32 \([0-9][0-9]*\).*/\1/p' || true)
if [[ -n $existing_ui_pid && -e /proc/$existing_ui_pid/exe ]]; then
  existing_ui_executable=$(readlink -f "/proc/$existing_ui_pid/exe" || true)
  existing_ui_command=$(tr '\0' '\n' < "/proc/$existing_ui_pid/cmdline" | head -1 || true)
  # After cmake --install atomically replaces the binary, /proc/PID/exe is
  # reported as "... (deleted)". argv[0] remains the exact installed path and
  # provides the second safe identity check needed during upgrades.
  if [[ $existing_ui_executable == "$prefix/bin/modern-ime-candidate-ui" ||
        $existing_ui_command == "$prefix/bin/modern-ime-candidate-ui" ]]; then
    kill "$existing_ui_pid"
    for _ in $(seq 1 20); do
      if ! kill -0 "$existing_ui_pid" 2>/dev/null; then break; fi
      sleep 0.05
    done
  fi
fi
systemctl --user restart modern-ime-service.service modern-ime-ui.service
if fcitx5-remote --check; then
  fcitx5-remote -e || true
  sleep 1
  XDG_DATA_DIRS="$ime_xdg_data_dirs" fcitx5 -d
  sleep 1
  fcitx5-remote -s modernime || true
  gdbus call --session --dest org.fcitx.Fcitx5 --object-path /controller --method org.fcitx.Fcitx.Controller1.Save >/dev/null || true
fi
printf 'Modern IME installed. Use Ctrl+Space or Fcitx input-method switcher to select “Modern IME”.\n'
printf 'Previous deployed files, when present, were backed up to %s.\n' "$deploy_backup"
