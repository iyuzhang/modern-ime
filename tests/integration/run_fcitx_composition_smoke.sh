#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
  printf 'usage: %s SOURCE_DIR ENGINE_LIBRARY CLIENT_EXECUTABLE CANDIDATE_UI_EXECUTABLE SERVICE_EXECUTABLE\n' "$0" >&2
  exit 64
fi

source_dir=$1
engine_library=${2%.so}
client_executable=$3
candidate_ui_executable=$4
service_executable=$5
smoke_root=$(mktemp -d)
fcitx_pid=
client_pid=
candidate_ui_pid=
service_pid=
cleanup() {
  for pid in "$client_pid" "$candidate_ui_pid" "$fcitx_pid" "$service_pid"; do
    if [[ -n $pid ]]; then kill "$pid" 2>/dev/null || true; fi
  done
  rm -rf "$smoke_root"
}
trap cleanup EXIT

mkdir -p "$smoke_root/config/fcitx5" "$smoke_root/data/fcitx5/addon" \
  "$smoke_root/data/fcitx5/inputmethod" "$smoke_root/state" "$smoke_root/bin"
install -m 0644 "$source_dir/tests/fixtures/fcitx-profile-modernime" \
  "$smoke_root/config/fcitx5/profile"
sed "s|^Library=.*|Library=$engine_library|" \
  "$source_dir/data/fcitx5/modernime.conf" > "$smoke_root/data/fcitx5/addon/modernime.conf"
install -m 0644 "$source_dir/data/fcitx5/modernime-inputmethod.conf" \
  "$smoke_root/data/fcitx5/inputmethod/modernime.conf"
ln -s "$candidate_ui_executable" "$smoke_root/bin/modern-ime-candidate-ui"

export XDG_CONFIG_HOME="$smoke_root/config"
export XDG_DATA_HOME="$smoke_root/data"
export XDG_STATE_HOME="$smoke_root/state"
export XDG_DATA_DIRS="$smoke_root/data:/usr/share"
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
export PATH="$smoke_root/bin:$PATH"

"$service_executable" >"$smoke_root/service.log" 2>&1 &
service_pid=$!
service_ready=
for _ in $(seq 1 30); do
  if gdbus call --session --dest org.freedesktop.DBus \
      --object-path /org/freedesktop/DBus \
      --method org.freedesktop.DBus.NameHasOwner org.modernime.Service1 \
      2>/dev/null | grep -q true; then
    service_ready=1
    break
  fi
  sleep 0.1
done
if [[ -z $service_ready ]]; then
  printf 'Modern IME service did not acquire its private D-Bus name.\n' >&2
  cat "$smoke_root/service.log" >&2
  exit 5
fi

fcitx5 -D --disable=all --enable=modernime,dbus,dbusfrontend,xim,keyboard \
  >"$smoke_root/fcitx.log" 2>&1 &
fcitx_pid=$!
sleep 1
"$candidate_ui_executable" >"$smoke_root/candidate-ui.log" 2>&1 &
candidate_ui_pid=$!
sleep 0.2
"$client_executable" >"$smoke_root/client.log" 2>&1 &
client_pid=$!

window_id=
for _ in $(seq 1 30); do
  window_id=$(xdotool search --name 'Modern IME Fcitx smoke' 2>/dev/null | head -1 || true)
  if [[ -n $window_id ]]; then break; fi
  sleep 0.1
done
if [[ -z $window_id ]]; then
  printf 'Qt smoke client did not create its window.\n' >&2
  cat "$smoke_root/client.log" >&2
  exit 2
fi

xdotool windowfocus --sync "$window_id"
sleep 1
active_input_method=
for _ in $(seq 1 20); do
  fcitx5-remote -o || true
  fcitx5-remote -s modernime || true
  active_input_method=$(fcitx5-remote -n 2>/dev/null || true)
  if [[ $active_input_method == modernime ]]; then break; fi
  sleep 0.1
done
if [[ $active_input_method != modernime ]]; then
  printf 'Modern IME did not become active in the private Fcitx session.\n' >&2
  cat "$smoke_root/fcitx.log" >&2
  exit 4
fi
xdotool type --window "$window_id" --delay 10 miaokongchumoban
# Page two, item six is the prefix candidate “妙”. The next candidate list
# must remain active; item five then selects “控触摸板” for the remaining input.
xdotool key --window "$window_id" Page_Down
xdotool key --window "$window_id" 6
xdotool key --window "$window_id" 5

# The segmented commit must also learn the complete reading/output pair and
# refresh the running engine. Wait until the asynchronous D-Bus write is
# observable before checking that the same running engine uses it.
learned_entries=
for _ in $(seq 1 30); do
  learned_entries=$(gdbus call --session \
    --dest org.modernime.Service1 \
    --object-path /org/modernime/Service1 \
    --method org.modernime.Service1.ListLexemes miaokongchumoban 2>/dev/null || true)
  if [[ $learned_entries == *'妙控触摸板'* ]]; then break; fi
  sleep 0.1
done
if [[ $learned_entries != *'妙控触摸板'* ]]; then
  printf 'The complete composed sentence was not persisted: %s\n' "$learned_entries" >&2
  exit 6
fi
sleep 0.2
xdotool type --window "$window_id" --delay 10 miaokongchumoban
xdotool key --window "$window_id" space

# High-confidence developer tokens commit as soon as they become unambiguous,
# hide the candidate window, and temporarily pass the rest of that token
# straight through. The following Space is only the normal word boundary.
xdotool type --window "$window_id" --delay 10 git
literal_status=
for _ in $(seq 1 20); do
  literal_status=$(gdbus call --session \
    --dest org.modernime.UI1 \
    --object-path /org/modernime/UI1 \
    --method org.modernime.UI1.Status 2>/dev/null || true)
  if [[ $literal_status == *'"visible":false'* && $literal_status == *'"preedit":""'* ]]; then break; fi
  sleep 0.02
done
if [[ $literal_status != *'"visible":false'* || $literal_status != *'"preedit":""'* ]]; then
  printf 'git did not leave automatic literal mode with a hidden candidate window: %s\n' "$literal_status" >&2
  exit 9
fi
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 status
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 ls
xdotool key --window "$window_id" comma
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 ll
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 React
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 'std::vector'
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 main
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 make
xdotool key --window "$window_id" 1
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 sha256
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 nihao
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 '/usr/local/bin'
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 -- '--help'
xdotool key --window "$window_id" space
xdotool type --window "$window_id" --delay 10 nihao
xdotool key --window "$window_id" period
xdotool type --window "$window_id" --delay 10 git
xdotool key --window "$window_id" Return

# A short Control press toggles a context-local English override. A normal
# Control shortcut must mark the modifier as used and must not toggle first.
xdotool key --window "$window_id" ctrl+c
xdotool key --window "$window_id" Control_L
xdotool type --window "$window_id" --delay 10 nihao
manual_status=$(gdbus call --session \
  --dest org.modernime.UI1 \
  --object-path /org/modernime/UI1 \
  --method org.modernime.UI1.Status 2>/dev/null || true)
if [[ $manual_status != *'"visible":false'* || $manual_status != *'"preedit":""'* ]]; then
  printf 'Short Control temporary English unexpectedly opened composition: %s\n' "$manual_status" >&2
  exit 10
fi
xdotool key --window "$window_id" space
xdotool key --window "$window_id" Control_L
xdotool type --window "$window_id" --delay 10 nihao
xdotool key --window "$window_id" space

# Explicit English mode remains available as a predictable escape hatch and
# forwards the original key stream without opening a composition.
english_config='{"protocol_version":1,"input_mode":"english"}'
updated=$(gdbus call --session \
  --dest org.modernime.Service1 \
  --object-path /org/modernime/Service1 \
  --method org.modernime.Service1.UpdateConfig "$english_config")
if [[ $updated != *true* ]]; then
  printf 'Could not switch the private session to English mode: %s\n' "$updated" >&2
  exit 8
fi
sleep 0.2
xdotool type --window "$window_id" --delay 10 EnglishMode

wait "$client_pid"
client_pid=
result=$(sed -n 's/^RESULT=//p' "$smoke_root/client.log")
return_count=$(sed -n 's/^RETURN_COUNT=//p' "$smoke_root/client.log")
expected='妙控触摸板妙控触摸板 git status ls, ll React std::vector main make sha256 你好/usr/local/bin --help 你好.gitnihao 你好EnglishMode'
if [[ $result != "$expected" ]]; then
  printf 'Expected %s, got %s\n' "$expected" "$result" >&2
  cat "$smoke_root/fcitx.log" >&2
  cat "$smoke_root/service.log" >&2
  exit 3
fi
if [[ $return_count != 1 ]]; then
  printf 'Expected one forwarded Return after a confident literal token, got %s\n' "$return_count" >&2
  cat "$smoke_root/fcitx.log" >&2
  exit 7
fi
printf 'fcitx_composition_result=%s\n' "$result"
