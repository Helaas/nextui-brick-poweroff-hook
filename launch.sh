#!/bin/sh
PAK_DIR="$(dirname "$0")"
PAK_NAME="$(basename "$PAK_DIR")"
PAK_NAME="${PAK_NAME%.*}"

# Set default LOGS_PATH if not provided by system
if [ -z "$LOGS_PATH" ]; then
    LOGS_PATH="/mnt/SDCARD/.userdata/tg5040/logs"
fi

# Ensure logs directory exists
mkdir -p "$LOGS_PATH" 2>/dev/null || true

# Enable debug tracing
set -x

# Redirect all output to log file
rm -f "$LOGS_PATH/$PAK_NAME.txt"
exec >>"$LOGS_PATH/$PAK_NAME.txt" 2>&1

echo "$0" "$@"
cd "$PAK_DIR" || exit 1

MODULE_NAME="poweroff_hook"
HUMAN_READABLE_NAME="Power-Off Hook"

# Script paths
BIN_DIR="$PAK_DIR/bin"

# Utility paths
JQ="$BIN_DIR/jq"
MINUI_LIST="$BIN_DIR/minui-list"
MINUI_PRESENTER="$BIN_DIR/minui-presenter"

show_message() {
    message="$1"
    seconds="$2"

    if [ -z "$seconds" ]; then
        seconds="forever"
    fi

    killall "$MINUI_PRESENTER" >/dev/null 2>&1 || true
    echo "$message" 1>&2
    if [ "$seconds" = "forever" ]; then
        "$MINUI_PRESENTER" --message "$message" --timeout -1 &
    else
        "$MINUI_PRESENTER" --message "$message" --timeout "$seconds"
    fi
}

disable_start_on_boot() {
    sed -i "/${PAK_NAME}.pak-on-boot/d" "$SDCARD_PATH/.userdata/$PLATFORM/auto.sh"
    sync
    return 0
}

enable_start_on_boot() {
    if [ ! -f "$SDCARD_PATH/.userdata/$PLATFORM/auto.sh" ]; then
        echo '#!/bin/sh' >"$SDCARD_PATH/.userdata/$PLATFORM/auto.sh"
        echo '' >>"$SDCARD_PATH/.userdata/$PLATFORM/auto.sh"
    fi

    echo "test -f \"\$SDCARD_PATH/Tools/\$PLATFORM/$PAK_NAME.pak/bin/on-boot\" && \"\$SDCARD_PATH/Tools/\$PLATFORM/$PAK_NAME.pak/bin/on-boot\" # ${PAK_NAME}.pak-on-boot" >>"$SDCARD_PATH/.userdata/$PLATFORM/auto.sh"
    chmod +x "$SDCARD_PATH/.userdata/$PLATFORM/auto.sh"
    sync
    return 0
}

will_start_on_boot() {
    if grep -q "${PAK_NAME}.pak-on-boot" "$SDCARD_PATH/.userdata/$PLATFORM/auto.sh" >/dev/null 2>&1; then
        return 0
    fi
    return 1
}

wait_for_service() {
    max_counter="$1"
    counter=0

    while ! "$BIN_DIR/service-is-running"; do
        counter=$((counter + 1))
        if [ "$counter" -gt "$max_counter" ]; then
            return 1
        fi
        sleep 1
    done
}

wait_for_service_to_stop() {
    max_counter="$1"
    counter=0

    while "$BIN_DIR/service-is-running"; do
        counter=$((counter + 1))
        if [ "$counter" -gt "$max_counter" ]; then
            return 1
        fi
        sleep 1
    done
}

current_settings() {
    minui_list_file="/tmp/${PAK_NAME}-settings.json"
    rm -f "$minui_list_file"

    "$JQ" -rM '{settings: .settings}' "$PAK_DIR/settings.json" >"$minui_list_file"
    if "$BIN_DIR/service-is-running"; then
        "$JQ" '.settings[0].selected = 1' "$minui_list_file" >"$minui_list_file.tmp"
        mv "$minui_list_file.tmp" "$minui_list_file"
    fi

    if will_start_on_boot; then
        "$JQ" '.settings[1].selected = 1' "$minui_list_file" >"$minui_list_file.tmp"
        mv "$minui_list_file.tmp" "$minui_list_file"
    fi

    cat "$minui_list_file"
}

main_screen() {
    settings="$1"
    minui_list_file="/tmp/${PAK_NAME}-minui-list.json"
    rm -f "$minui_list_file"

    echo "$settings" >"$minui_list_file"

    "$MINUI_LIST" --disable-auto-sleep --file "$minui_list_file" --format json --title "$HUMAN_READABLE_NAME" --confirm-text "SAVE" --item-key "settings" --write-value state
}

cleanup() {
    rm -f "/tmp/${PAK_NAME}-old-settings.json"
    rm -f "/tmp/${PAK_NAME}-new-settings.json"
    rm -f "/tmp/${PAK_NAME}-settings.json"
    rm -f "/tmp/${PAK_NAME}-minui-list.json"
    rm -f /tmp/stay_awake
    killall "$(basename "$MINUI_PRESENTER")" >/dev/null 2>&1 || true
}

main() {
    echo "1" >/tmp/stay_awake
    trap "cleanup" EXIT INT TERM HUP QUIT

    if [ "$PLATFORM" = "tg3040" ] && [ -z "$DEVICE" ]; then
        export DEVICE="brick"
        export PLATFORM="tg5040"
    fi

    allowed_platforms="tg5040"
    if ! echo "$allowed_platforms" | grep -q "$PLATFORM"; then
        show_message "$PLATFORM is not a supported platform" 2
        return 1
    fi

    if [ ! -f "$MINUI_LIST" ]; then
        show_message "minui-list not found" 2
        return 1
    fi

    if [ ! -f "$MINUI_PRESENTER" ]; then
        show_message "minui-presenter not found" 2
        return 1
    fi

    if [ ! -f "$JQ" ]; then
        show_message "jq not found" 2
        return 1
    fi

    # Make all scripts and utilities executable
    chmod +x "$PAK_DIR/bin/"* 2>/dev/null || true

    while true; do
        settings="$(current_settings)"
        new_settings="$(main_screen "$settings")"
        exit_code=$?
        # exit codes: 2 = back button, 3 = menu button
        if [ "$exit_code" -ne 0 ]; then
            break
        fi

        echo "$settings" >"/tmp/${PAK_NAME}-old-settings.json"
        echo "$new_settings" >"/tmp/${PAK_NAME}-new-settings.json"

        old_enabled="$("$JQ" -rM '.settings[0].selected' "/tmp/${PAK_NAME}-old-settings.json")"
        enabled="$("$JQ" -rM '.settings[0].selected' "/tmp/${PAK_NAME}-new-settings.json")"

        old_start_on_boot="$("$JQ" -rM '.settings[1].selected' "/tmp/${PAK_NAME}-old-settings.json")"
        start_on_boot="$("$JQ" -rM '.settings[1].selected' "/tmp/${PAK_NAME}-new-settings.json")"

        if [ "$old_enabled" != "$enabled" ]; then
            if [ "$enabled" = "1" ]; then
                show_message "Loading $HUMAN_READABLE_NAME module" 2
                if ! "$BIN_DIR/service-on"; then
                    show_message "Failed to load $HUMAN_READABLE_NAME module!" 2
                    continue
                fi

                show_message "Waiting for $HUMAN_READABLE_NAME to be active" forever
                if ! wait_for_service 5; then
                    show_message "Failed to verify $HUMAN_READABLE_NAME" 2
                fi
                killall "$(basename "$MINUI_PRESENTER")" >/dev/null 2>&1 || true
            else
                show_message "Unloading $HUMAN_READABLE_NAME module" 2
                if ! "$BIN_DIR/service-off"; then
                    show_message "Failed to unload $HUMAN_READABLE_NAME module!" 2
                fi

                show_message "Waiting for $HUMAN_READABLE_NAME to stop" forever
                if ! wait_for_service_to_stop 5; then
                    show_message "Failed to unload $HUMAN_READABLE_NAME" 2
                fi
                killall minui-presenter >/dev/null 2>&1 || true
            fi
        fi

        if [ "$old_start_on_boot" != "$start_on_boot" ]; then
            if [ "$start_on_boot" = "1" ]; then
                show_message "Enabling start on boot" 2
                if ! enable_start_on_boot; then
                    show_message "Failed to enable start on boot!" 2
                fi
            else
                show_message "Disabling start on boot" 2
                if ! disable_start_on_boot; then
                    show_message "Failed to disable start on boot!" 2
                fi
            fi
        fi
    done
}

main "$@"
