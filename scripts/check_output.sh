#!/bin/bash

#sshpass -p $(cat password) rsync -avzP -e ssh nvidia-vm.home:/home/jonathan/Projects/pc-final-project/output/omp.hd5 /home/jonathan/Projects/pc-final-project/output
source .venv/bin/activate
python3 tools/validate_particles_h5.py "$1" "$2"
