#!/usr/bin/env bash
# clean.sh <pkg>: delete the bazel-out of re/<pkg>
D="$(dirname "$0")"
bash $D/sv.sh "P=<build-dir> if [ -d \$P ]; then chmod -R u+w \$P; rm -rf \$P; echo removed \$P; fi; df -h / | tail -1"
