#!/bin/sh
# mrcs one-line installer script
set -e

# GitHub repository details
REPO="estiaksoyeb/mrcs"

echo "==============================================="
echo " Installing Modern Revision Control System (mrcs)"
echo "==============================================="

# 1. Detect OS
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
case "$OS" in
    linux)  OS_NAME="linux" ;;
    darwin) OS_NAME="darwin" ;;
    *)
        echo "Error: Unsupported OS '$OS'" >&2
        exit 1
        ;;
esac

# 2. Detect Architecture
ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64)   ARCH_NAME="amd64" ;;
    arm64|aarch64)  ARCH_NAME="arm64" ;;
    *)
        echo "Error: Unsupported Architecture '$ARCH'" >&2
        exit 1
        ;;
esac

# 3. Check and install RCS dependency if missing
if ! command -v rcs >/dev/null 2>&1; then
    echo "-----------------------------------------------"
    echo " WARNING: Dependency 'rcs' is missing!"
    echo "-----------------------------------------------"
    echo "mrcs wraps GNU RCS and requires it to run."
    echo ""
    
    # Check if we can read from terminal for auto-install
    AUTO_INSTALLED=false
    if [ -t 1 ] && [ -c /dev/tty ]; then
        printf "Would you like to install 'rcs' automatically now? [y/N]: "
        read -r ANSWER < /dev/tty
        if [ "$ANSWER" = "y" ] || [ "$ANSWER" = "Y" ]; then
            echo "Attempting to install 'rcs'..."
            if [ "$OS_NAME" = "darwin" ]; then
                brew install rcs
                AUTO_INSTALLED=true
            elif [ -n "$PREFIX" ]; then
                pkg install -y rcs
                AUTO_INSTALLED=true
            elif command -v apt-get >/dev/null 2>&1; then
                if [ "$(id -u)" -eq 0 ]; then
                    apt-get update && apt-get install -y rcs
                else
                    sudo apt-get update && sudo apt-get install -y rcs
                fi
                AUTO_INSTALLED=true
            elif command -v dnf >/dev/null 2>&1; then
                if [ "$(id -u)" -eq 0 ]; then
                    dnf install -y rcs
                else
                    sudo dnf install -y rcs
                fi
                AUTO_INSTALLED=true
            fi
        fi
    fi
    
    if [ "$AUTO_INSTALLED" = false ]; then
        echo "Please install 'rcs' using your package manager:"
        if [ "$OS_NAME" = "darwin" ]; then
            echo "  brew install rcs"
        elif [ -n "$PREFIX" ]; then
            echo "  pkg install rcs"
        else
            echo "  sudo apt install rcs  # Debian/Ubuntu"
            echo "  sudo dnf install rcs  # Fedora/RHEL"
        fi
        echo "-----------------------------------------------"
        echo ""
    fi
fi

# 4. Determine Installation Path
# Termux check: Android/Termux environments have $PREFIX set
if [ -n "$PREFIX" ] && [ -d "$PREFIX/bin" ]; then
    INSTALL_DIR="$PREFIX/bin"
    USE_SUDO=false
elif [ -w "/usr/local/bin" ]; then
    INSTALL_DIR="/usr/local/bin"
    USE_SUDO=false
else
    INSTALL_DIR="/usr/local/bin"
    # If we need sudo, check if sudo is available
    if command -v sudo >/dev/null 2>&1; then
        USE_SUDO=true
    else
        USE_SUDO=false
        # Fallback to user home directory if not root/sudo-capable
        INSTALL_DIR="$HOME/bin"
        mkdir -p "$INSTALL_DIR"
        echo "Warning: /usr/local/bin is write-protected and 'sudo' is not available."
        echo "Installing to $INSTALL_DIR instead. Please ensure it is in your PATH."
    fi
fi

# 4. Perform Download
ASSET_NAME="mrcs-${OS_NAME}-${ARCH_NAME}"
DOWNLOAD_URL="https://github.com/${REPO}/releases/latest/download/${ASSET_NAME}"
TEMP_FILE=$(mktemp)

echo "Downloading ${ASSET_NAME} from GitHub Releases..."
if command -v curl >/dev/null 2>&1; then
    curl -fsSL -o "$TEMP_FILE" "$DOWNLOAD_URL"
elif command -v wget >/dev/null 2>&1; then
    wget -qO "$TEMP_FILE" "$DOWNLOAD_URL"
else
    echo "Error: Neither 'curl' nor 'wget' was found. Please install one of them." >&2
    rm -f "$TEMP_FILE"
    exit 1
fi

# 5. Move binary to installation path
echo "Installing to ${INSTALL_DIR}/mrcs..."
chmod +x "$TEMP_FILE"
if [ "$USE_SUDO" = true ]; then
    sudo mv "$TEMP_FILE" "${INSTALL_DIR}/mrcs"
else
    mv "$TEMP_FILE" "${INSTALL_DIR}/mrcs"
fi

# 6. Install Bash Autocompletion
COMPLETION_DIR=""
if [ -n "$PREFIX" ] && [ -d "$PREFIX/share/bash-completion/completions" ]; then
    COMPLETION_DIR="$PREFIX/share/bash-completion/completions"
elif [ -d "/usr/share/bash-completion/completions" ] && [ -w "/usr/share/bash-completion/completions" ]; then
    COMPLETION_DIR="/usr/share/bash-completion/completions"
elif [ -d "/etc/bash_completion.d" ] && [ -w "/etc/bash_completion.d" ]; then
    COMPLETION_DIR="/etc/bash_completion.d"
fi

if [ -n "$COMPLETION_DIR" ]; then
    echo "Installing Bash autocompletion..."
    COMP_URL="https://raw.githubusercontent.com/${REPO}/main/mrcs.bash"
    TEMP_COMP=$(mktemp)
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$TEMP_COMP" "$COMP_URL" || true
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$TEMP_COMP" "$COMP_URL" || true
    fi
    if [ -s "$TEMP_COMP" ]; then
        if [ "$USE_SUDO" = true ]; then
            sudo mv "$TEMP_COMP" "${COMPLETION_DIR}/mrcs"
        else
            mv "$TEMP_COMP" "${COMPLETION_DIR}/mrcs"
        fi
        echo "Bash autocompletion installed in ${COMPLETION_DIR}/mrcs."
    else
        rm -f "$TEMP_COMP"
    fi
fi

# 7. Final Verification
if command -v rcs >/dev/null 2>&1; then
    echo "Success! mrcs is installed and ready to use."
else
    echo "mrcs is installed, but requires 'rcs' to be installed before you can run it."
fi
