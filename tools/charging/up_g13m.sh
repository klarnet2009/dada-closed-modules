#!/bin/bash
cd "/e/<workdir>/work/re/src"
tar c stubs_g13a | bash ../g13m_tools/sv.sh "tar x -C <build-dir>" 2>&1 | grep -v "in the future"
