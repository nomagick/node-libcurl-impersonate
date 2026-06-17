#!/usr/bin/env bash
set -euo pipefail
cd "$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd -P)"

# Configuration variables
CURL_IMPERSONATE_DIR="$(dirname "$PWD")/deps/curl-impersonate"
BUILD_DIR="$CURL_IMPERSONATE_DIR/build"
INSTALL_DIR="$BUILD_DIR/install"
SRC_ARTIFACTS_FILE="$BUILD_DIR/curl-impersonate.src.tar.gz"
BUILD_ARTIFACTS_FILE="$BUILD_DIR/curl-impersonate.tar.gz"
OS=$(uname -s)
ARCH=$(uname -m)

CURL_VERSION="8_15_0"
CURL_SRC_DIR="$BUILD_DIR/curl-$CURL_VERSION"
CURL_OUT_DIR="$BUILD_DIR/curl-impersonate"

# Determine OS-specific variables
if [ "$OS" = "Linux" ]; then
  MAKE="make"
  HOST="x86_64-linux-gnu"
  CPP_LIB="stdc++"
elif [ "$OS" = "Darwin" ]; then
  MAKE="gmake"
  HOST="arm64-apple-darwin"
  CPP_LIB="c++"
else
  echo "Unsupported operating system: $OS"
  exit 1
fi

cmake_args="-G Ninja -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR"
# Configure
configure_build() {
  if [ "$OS" = "Linux" ]; then
   cmake_args="$cmake_args -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=$ARCH"
   cmake_args="$cmake_args -DCURL_CA_PATH=/etc/ssl/certs -DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt"
  fi

  if [ "$OS" = "Linux" ]; then
   $MAKE build prepare-libidn2 BUILD_DIR="$BUILD_DIR"
  fi

  $MAKE configure BUILD_DIR="$BUILD_DIR" CMAKE_CONFIGURE_ARGS="$cmake_args"
}

# Build Curl Impersonate
build_curl_impersonate() {
  $MAKE build BUILD_DIR="$BUILD_DIR" CMAKE_CONFIGURE_ARGS="$cmake_args"
  $MAKE checkbuild BUILD_DIR="$BUILD_DIR" CMAKE_CONFIGURE_ARGS="$cmake_args"
  $MAKE install-strip BUILD_DIR="$BUILD_DIR" CMAKE_CONFIGURE_ARGS="$cmake_args"

  # copy curl include dir
  mkdir -p "$CURL_OUT_DIR/include"
  cp -r "$INSTALL_DIR/include/curl" "$CURL_OUT_DIR/include"
  rm -f "$CURL_OUT_DIR/include/curl/".* 2>/dev/null || true
}

# Main execution
main() {
  echo "Starting curl-impersonate build process..."

  # Create install directory
  mkdir -p "$BUILD_DIR"
  mkdir -p "$INSTALL_DIR"
  cd "$CURL_IMPERSONATE_DIR"
  # Configure
  echo "Configuring build..."
  configure_build

  # Build curl impersonate
  echo "Building Curl Impersonate..."
  build_curl_impersonate

  echo "Build process completed successfully!"
}

main
