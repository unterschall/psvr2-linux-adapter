#!/bin/bash
# Make script files executable

set -e

# Make main scripts executable
chmod +x configure.sh
chmod +x build.sh
chmod +x setup.sh

# Make tool scripts executable
chmod +x tools/*.sh

# Make kernel module scripts executable
chmod +x kernel/module/*.sh

# Make this script executable too
chmod +x "$0"

echo "All scripts are now executable!"
