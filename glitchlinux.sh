#!/bin/sh

set -u

REPOSITORY=${GLITCHLINUX_REPOSITORY:-Minecraft-Mod-Coder/GlitchLinux-GameCube-Edition-}
API_URL="https://api.github.com/repos/$REPOSITORY"
REPOSITORY_URL="https://github.com/$REPOSITORY"

pause() {

    printf '\nPress Enter to continue...'
    IFS= read -r _ || true
}

show_desktop() {
    clear 2>/dev/null || true
    printf '%s\n' 'GLITCHLINUX WORKBENCH' '=====================' ''
    printf '%s\n' '  1  Terminal' '  2  Browser' '  3  Settings' '  4  GitHub services' '  q  Shut down workbench' ''
}

open_terminal() {
    shell=${SHELL:-/bin/sh}
    printf 'Starting %s. Type exit to return to the workbench.\n' "$shell"
    "$shell" -i
}

open_browser() {
    url=${1:-}
    if [ -z "$url" ]; then
        printf 'URL (Enter for project page): '
        IFS= read -r url || return
        url=${url:-$REPOSITORY_URL}
    fi

    case "$url" in
        http://*|https://*) ;;
        *) printf '%s\n' 'Please enter an http:// or https:// URL.'; pause; return ;;
    esac

    if command -v dialog >/dev/null 2>&1 && [ -t 0 ] && [ -t 1 ]; then
        browser_mode=$(dialog --clear --stdout --mouse --title 'Browser' \
            --menu 'Choose a browser' 12 58 2 \
            text 'Terminal browser' system 'System browser') || return
    else
        browser_mode=text
    fi

    case "$browser_mode" in
        text)
            if command -v lynx >/dev/null 2>&1; then lynx "$url"
            elif command -v w3m >/dev/null 2>&1; then w3m "$url"
            elif command -v links >/dev/null 2>&1; then links "$url"
            else
                printf '%s\n' 'Install lynx, w3m, or links for terminal browsing.'
                pause
            fi
            ;;
        system)
            if command -v xdg-open >/dev/null 2>&1; then
                xdg-open "$url" >/dev/null 2>&1 &
            else
                printf '%s\n' 'xdg-open is not installed.'
                pause
            fi
            ;;
    esac
}

show_input_devices() {
    printf '%s\n' 'Detected input devices:'
    if [ -d /dev/input/by-id ]; then
        found=no
        for device in /dev/input/by-id/*; do
            [ -e "$device" ] || continue
            printf '  %s -> %s\n' "${device##*/}" "$(readlink "$device")"
            found=yes
        done
        if [ "$found" = no ]; then printf '%s\n' '  No named devices found.'; fi
    elif [ -r /proc/bus/input/devices ]; then
        sed -n 's/^N: Name=/  /p' /proc/bus/input/devices
    else
        printf '%s\n' '  Linux input device information is unavailable.'
    fi
    pause
}

settings_menu() {
    while :; do
        if command -v dialog >/dev/null 2>&1 && [ -t 0 ] && [ -t 1 ]; then
            setting=$(dialog --clear --stdout --mouse --title 'Settings' \
                --menu 'System controls' 16 58 6 \
                1 'Show system volume' 2 'Raise volume' 3 'Lower volume' \
                4 'Toggle mute' 5 'Show keyboard/mouse devices' b 'Back') || return
        else
            clear 2>/dev/null || true
            printf '%s\n' 'SETTINGS' '========' '' '  1  Show system volume' '  2  Raise volume' '  3  Lower volume' '  4  Toggle mute' '  5  Show keyboard/mouse devices' '  b  Back' ''
            printf 'Select: '
            IFS= read -r setting || return
        fi
        case "$setting" in
            1)
                if command -v amixer >/dev/null 2>&1; then amixer sget Master; else printf '%s\n' 'ALSA amixer is not installed.'; fi
                pause
                ;;
            2|3|4)
                if ! command -v amixer >/dev/null 2>&1; then
                    printf '%s\n' 'ALSA amixer is not installed.'
                elif [ "$setting" = 2 ]; then
                    amixer -q sset Master 5%+
                elif [ "$setting" = 3 ]; then
                    amixer -q sset Master 5%-
                else
                    amixer -q sset Master toggle
                fi
                ;;
            5) show_input_devices ;;
            b|B) return ;;
            *) printf '%s\n' 'Choose one of the listed settings.'; pause ;;
        esac
    done
}

show_repository_info() {
    if ! command -v curl >/dev/null 2>&1; then
        printf '%s\n' 'curl is required for the GitHub connection.'
    else
        response=$(curl --fail --silent --show-error --location \
        --header 'Accept: application/vnd.github+json' \
        "$API_URL") || {
            printf '%s\n' 'GitHub API request failed. Check the network and repository name.'
            pause
            return
        }
        if command -v jq >/dev/null 2>&1; then
            printf '%s\n' "$response" | jq -r '"Repository: \(.full_name)\nDescription: \(.description // "No description")\nDefault branch: \(.default_branch)\nStars: \(.stargazers_count)  Forks: \(.forks_count)"'
        else
            printf 'GitHub API is reachable. Repository: %s\nInstall jq to show repository details.\n' "$REPOSITORY_URL"
        fi
    fi
    pause
}

show_recent_releases() {
    if ! command -v curl >/dev/null 2>&1; then
        printf '%s\n' 'curl is required for the GitHub connection.'
    elif ! command -v jq >/dev/null 2>&1; then
        printf '%s\n' 'Install jq to read release information from GitHub.'
    else
        response=$(curl --fail --silent --show-error --location \
            --header 'Accept: application/vnd.github+json' \
            "$API_URL/releases?per_page=5") || {
            printf '%s\n' 'Could not retrieve releases from GitHub.'
            pause
            return
        }
        printf '%s\n' "$response" | jq -r 'if length == 0 then "No published releases." else .[] | "\(.tag_name)  \(.name // "")\n\(.html_url)\n" end'
    fi
    pause
}

github_services() {
    while :; do
        if command -v dialog >/dev/null 2>&1 && [ -t 0 ] && [ -t 1 ]; then
            github_action=$(dialog --clear --stdout --mouse --title 'GitHub' \
                --menu "$REPOSITORY" 14 66 4 \
                1 'Repository details' 2 'Recent releases' \
                3 'Open project page' b 'Back') || return
        else
            clear 2>/dev/null || true
            printf 'GITHUB: %s\n\n' "$REPOSITORY"
            printf '%s\n' '  1  Repository details' '  2  Recent releases' '  3  Open project page' '  b  Back' ''
            printf 'Select: '
            IFS= read -r github_action || return
        fi
        case "$github_action" in
            1) show_repository_info ;;
            2) show_recent_releases ;;
            3) open_browser "$REPOSITORY_URL" ;;
            b|B) return ;;
            *) printf '%s\n' 'Choose one of the listed options.'; pause ;;
        esac
    done
}

while :; do
    if command -v dialog >/dev/null 2>&1 && [ -t 0 ] && [ -t 1 ]; then
        action=$(dialog --clear --stdout --mouse --title 'GlitchLinux Workbench' \
            --menu 'Desktop' 15 58 5 \
            1 'Terminal' 2 'Browser' 3 'Settings' 4 'GitHub services' \
            q 'Shut down workbench') || exit 0
    else
        show_desktop
        printf 'Select: '
        IFS= read -r action || exit 0
    fi
    case "$action" in
        1) open_terminal ;;
        2) open_browser ;;
        3) settings_menu ;;
        4) github_services ;;
        q|Q) printf '%s\n' 'Workbench closed.'; exit 0 ;;
        *) printf '%s\n' 'Choose one of the listed options.'; pause ;;
    esac
done