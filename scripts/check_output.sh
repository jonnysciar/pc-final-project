#!/bin/bash

sshpass -p '******' rsync -avzP -e ssh nvidia-vm.home:/home/jonathan/Projects/pc-final-project/output/* /home/jonathan/Projects/pc-final-project/output
source .venv/bin/activate
python3 tools/validate_particles_h5.py "$1" "$2"
