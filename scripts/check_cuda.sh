#!/bin/bash

sshpass -p 'kyra1907' rsync -avzP -e ssh nvidia-vm.home:/home/jonathan/Projects/pc-final-project/output/* /home/jonathan/Projects/pc-final-project/output
source .venv/bin/activate
python3 tools/validate_particles_h5.py output/reference.hd5 output/cuda.hd5
