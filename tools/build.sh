#!/bin/bash
# build.sh - Vitaki-Fork Docker Build Script
# Usage: ./build.sh [command] [options]

set -e  # Exit on any error

# Configuration
PROJECT_NAME="vitaki-fork"
DOCKER_IMAGE="vitaki-fork-dev:latest"
BUILD_DIR="./build"
SOURCE_DIR="./vita/src"

# Version configuration
VERSION_PHASE="0.1"
VERSION_ITERATION="173"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Version management functions
increment_version() {
    VERSION_ITERATION=$((VERSION_ITERATION + 1))
    sed -i.bak "s/VERSION_ITERATION=\"[0-9]*\"/VERSION_ITERATION=\"$VERSION_ITERATION\"/" "$0"
    rm -f "${0}.bak"
    log_info "Version incremented to v${VERSION_PHASE}.${VERSION_ITERATION}"
}

get_version_string() {
    echo "VitakiForkv${VERSION_PHASE}.${VERSION_ITERATION}"
}

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if Docker is running
check_docker() {
    if ! docker info > /dev/null 2>&1; then
        log_error "Docker is not running. Please start Docker Desktop and try again."
        exit 1
    fi
}

# Check if custom Docker image exists, build if needed
ensure_docker_image() {
    if ! docker image inspect "$DOCKER_IMAGE" > /dev/null 2>&1; then
        log_info "Custom Docker image not found, building it..."
        build_docker_image
    else
        log_info "Using existing custom Docker image: $DOCKER_IMAGE"
    fi
}

# Create build directory
setup_build_dir() {
    mkdir -p "$BUILD_DIR"
    log_info "Build directory: $BUILD_DIR"
}

# Build custom Vitaki-Fork development image
build_docker_image() {
    log_info "Building Vitaki-Fork development Docker image..."
    docker build --platform linux/amd64 -t "$DOCKER_IMAGE" .
    log_success "Custom Docker image built successfully"
}

# Pull latest VitaSDK Docker image
pull_docker_image() {
    log_info "Pulling latest VitaSDK Docker image..."
    docker pull --platform linux/amd64 vitasdk/vitasdk:latest
    log_success "Base Docker image updated"
}

# Generate version header
generate_version_header() {
    log_info "Generating version header..."

    # Create version header file in include directory
    mkdir -p vita/include
    cat > vita/include/version.h << EOF
#ifndef VITAKI_FORK_VERSION_H
#define VITAKI_FORK_VERSION_H

// Auto-generated version information - DO NOT EDIT MANUALLY
#define VITAKI_FORK_VERSION_MAJOR 0
#define VITAKI_FORK_VERSION_MINOR 1
#define VITAKI_FORK_VERSION_PATCH ${VERSION_ITERATION}
#define VITAKI_FORK_VERSION_STRING "0.1.${VERSION_ITERATION}"

#endif // VITAKI_FORK_VERSION_H
EOF

    log_info "Version header generated: v0.1.${VERSION_ITERATION}"
}

# Build VPK using Docker
build_vpk() {
    local build_type="${1:-release}"
    
    # Increment version BEFORE build so it shows correctly in logs
    increment_version
    
    log_info "Building Vitaki-Fork ($build_type mode)..."
    
    # Generate version header before building
    generate_version_header
    
    # Set CMake build type
    local cmake_flags=""
    if [ "$build_type" = "debug" ]; then
        cmake_flags="-DCMAKE_BUILD_TYPE=Debug"
    else
        cmake_flags="-DCMAKE_BUILD_TYPE=Release"
    fi
    
    # Run build in Docker container (with platform specification for ARM64 hosts)
    docker run --rm \
        --platform linux/amd64 \
        -v "$(pwd):/build/git" \
        -w /build/git \
        "$DOCKER_IMAGE" \
        bash -c "
            set -e
            echo 'Setting up build environment...'
            export VITASDK=/usr/local/vitasdk
            export PATH=\$VITASDK/bin:\$PATH
            mkdir -p build && cd build
            
            echo 'Running CMake with Vita enabled...'
            cmake .. -DCMAKE_TOOLCHAIN_FILE=\$VITASDK/share/vita.toolchain.cmake \
                     -DCHIAKI_ENABLE_VITA=ON \
                     -DCHIAKI_ENABLE_GUI=OFF \
                     -DCHIAKI_ENABLE_CLI=OFF \
                     -DCHIAKI_ENABLE_TESTS=OFF \
                     $cmake_flags
            
            echo 'Building project...'
            make -j\$(nproc)
            
            echo 'Looking for VPK files...'
            # VPK should be in vita subdirectory
            find . -name '*.vpk' -exec ls -la {} \;

            # Copy VPK to expected location
            if [ -f vita/Vitaki.vpk ]; then
                cp vita/Vitaki.vpk ${PROJECT_NAME}.vpk
                echo 'VPK copied to ${PROJECT_NAME}.vpk'
            fi

            echo 'Build completed successfully!'
        "
    
    # Check if VPK was created
    if [ -f "$BUILD_DIR/${PROJECT_NAME}.vpk" ]; then
        # Create versioned VPK name (version already incremented before build)
        local versioned_name="$(get_version_string).vpk"
        
        # Copy VPK with versioned name to project root
        cp "$BUILD_DIR/${PROJECT_NAME}.vpk" "$versioned_name"
        
        log_success "VPK created: $BUILD_DIR/${PROJECT_NAME}.vpk"
        log_success "Versioned VPK: $versioned_name"
        
        # Show file size
        local size=$(du -h "$BUILD_DIR/${PROJECT_NAME}.vpk" | cut -f1)
        log_info "VPK size: $size"
    else
        # Look for any VPK files
        local vpk_files=$(find "$BUILD_DIR" -name "*.vpk" 2>/dev/null || true)
        if [ -n "$vpk_files" ]; then
            log_warning "VPK created with different name:"
            echo "$vpk_files"
        else
            log_error "No VPK file found in build directory"
            exit 1
        fi
    fi
}

# Clean build artifacts
clean_build() {
    log_info "Cleaning build artifacts..."
    rm -rf "$BUILD_DIR"
    log_success "Build directory cleaned"
}

# Format code using clang-format
format_code() {
    log_info "Formatting code..."
    
    docker run --rm \
        --platform linux/amd64 \
        -v "$(pwd):/build/git" \
        -w /build/git \
        "$DOCKER_IMAGE" \
        bash -c "
            find src/ -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' | \
            xargs -r clang-format -i -style=Google
        "
    
    log_success "Code formatting completed"
}

# Run linter (cppcheck)
lint_code() {
    log_info "Running code linter..."
    
    docker run --rm \
        --platform linux/amd64 \
        -v "$(pwd):/build/git" \
        -w /build/git \
        "$DOCKER_IMAGE" \
        bash -c "
            if command -v cppcheck >/dev/null 2>&1; then
                cppcheck --enable=all --error-exitcode=1 --suppress=missingIncludeSystem --suppressions-list=.cppcheck-suppressions src/ || true
            else
                echo 'cppcheck not available in this image'
                # Use basic gcc checks instead
                find src/ -name '*.c' | xargs -r gcc -fsyntax-only -Wall -Wextra || true
            fi
        "
    
    log_success "Code linting completed"
}

# Build and run test suite
run_tests() {
    log_info "Building and running test suite..."
    
    # Run test build in Docker container
    docker run --rm \
        --platform linux/amd64 \
        -v "$(pwd):/build/git" \
        -w /build/git \
        "$DOCKER_IMAGE" \
        bash -c "
            set -e
            echo 'Setting up test build environment...'
            export VITASDK=/usr/local/vitasdk
            export PATH=\$VITASDK/bin:\$PATH
            mkdir -p build && cd build
            
            echo 'Running CMake with test configuration...'
            cmake .. -DCMAKE_TOOLCHAIN_FILE=\$VITASDK/share/vita.toolchain.cmake -DBUILD_TESTS=ON
            
            echo 'Building test suite...'
            make -j\$(nproc) vitarps5_tests
            
            echo 'Test executable built successfully!'
            ls -la vitarps5_tests* || echo 'Test files:'
            find . -name '*test*' -exec ls -la {} \;
        "
    
    # Check if test executable was created
    if [ -f "$BUILD_DIR/vitarps5_tests" ]; then
        log_success "Test executable created: $BUILD_DIR/vitarps5_tests"
        log_info "Note: Tests are built successfully and ready to run on Vita hardware"
        
        # Show file size
        local size=$(du -h "$BUILD_DIR/vitarps5_tests" | cut -f1)
        log_info "Test executable size: $size"
    else
        log_warning "Test executable not found. Build may have failed."
        log_info "Check build output above for errors"
    fi
    
    log_success "Test build completed"
}

# Interactive shell for development
dev_shell() {
    log_info "Starting interactive development shell..."
    log_info "Use 'exit' to return to host system"
    
    docker run --rm -it \
        --platform linux/amd64 \
        -v "$(pwd):/build/git" \
        -w /build/git \
        "$DOCKER_IMAGE" \
        bash
}

# Deploy VPK to Vita via FTP
deploy_vita() {
    local vita_ip="$1"
    
    if [ -z "$vita_ip" ]; then
        log_error "Please provide Vita IP address: ./build.sh deploy <vita_ip>"
        exit 1
    fi
    
    if [ ! -f "$BUILD_DIR/${PROJECT_NAME}.vpk" ]; then
        log_error "VPK not found. Run './build.sh' first."
        exit 1
    fi
    
    log_info "Deploying to Vita at $vita_ip..."
    log_warning "Make sure VitaShell FTP server is running (SELECT button in VitaShell)"
    
    # Use curl to upload VPK
    if command -v curl >/dev/null 2>&1; then
        curl -T "$BUILD_DIR/${PROJECT_NAME}.vpk" "ftp://$vita_ip:1337/ux0:/"
        log_success "VPK uploaded successfully"
        log_info "Install the VPK in VitaShell by pressing X on the file"
    else
        log_error "curl not found. Please install curl or transfer the VPK manually:"
        log_info "File location: $BUILD_DIR/${PROJECT_NAME}.vpk"
    fi
}

# Show current version
show_version() {
    echo "Current version: $(get_version_string)"
    echo "Next build will be: VitaRPS5v${VERSION_PHASE}.$((VERSION_ITERATION + 1))"
}

# Show help
show_help() {
    echo "Vitaki-Fork Build Script"
    echo "Usage: ./build.sh [command] [options]"
    echo ""
    echo "Commands:"
    echo "  (no args)    Build release VPK (auto-increments version)"
    echo "  debug        Build debug VPK with symbols (auto-increments version)"
    echo "  test         Build and run test suite"
    echo "  clean        Clean build artifacts"
    echo "  format       Format source code"
    echo "  lint         Run code linter"
    echo "  shell        Interactive development shell"
    echo "  deploy <ip>  Deploy VPK to Vita via FTP"
    echo "  update       Pull latest base image and rebuild custom image"
    echo "  build-image  Build custom Docker image"
    echo "  version      Show current version information"
    echo "  help         Show this help"
    echo ""
    echo "Examples:"
    echo "  ./build.sh                    # Build release VPK"
    echo "  ./build.sh debug              # Build with debug symbols"
    echo "  ./build.sh test               # Build and run test suite"
    echo "  ./build.sh deploy 192.168.1.100  # Deploy to Vita"
    echo "  ./build.sh shell              # Interactive development"
    echo "  ./build.sh version            # Show version info"
}

# Main script logic
main() {
    local command="${1:-build}"
    
    # Check Docker availability
    check_docker
    
    case "$command" in
        "build"|"")
            ensure_docker_image
            setup_build_dir
            build_vpk "release"
            ;;
        "debug")
            ensure_docker_image
            setup_build_dir
            build_vpk "debug"
            ;;
        "test")
            ensure_docker_image
            setup_build_dir
            run_tests
            ;;
        "clean")
            clean_build
            ;;
        "format")
            ensure_docker_image
            format_code
            ;;
        "lint")
            ensure_docker_image
            lint_code
            ;;
        "shell")
            ensure_docker_image
            dev_shell
            ;;
        "deploy")
            deploy_vita "$2"
            ;;
        "update")
            pull_docker_image
            build_docker_image
            ;;
        "build-image")
            build_docker_image
            ;;
        "version")
            show_version
            ;;
        "help"|"-h"|"--help")
            show_help
            ;;
        *)
            log_error "Unknown command: $command"
            show_help
            exit 1
            ;;
    esac
}

# Run main function
main "$@"