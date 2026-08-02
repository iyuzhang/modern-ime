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
backup_deployed_file "$fcitx_library_dir/libmodernime.so" system-libmodernime.so
backup_deployed_file "$fcitx_library_dir/libmodernimeui.so" system-libmodernimeui.so

cmake --install "$build_dir" --prefix "$prefix"
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
XDG_DATA_DIRS="$ime_xdg_data_dirs" systemctl --user import-environment XDG_DATA_DIRS
XDG_DATA_DIRS="$ime_xdg_data_dirs" dbus-update-activation-environment --systemd XDG_DATA_DIRS
systemctl --user enable modern-ime-service.service modern-ime-ui.service
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
