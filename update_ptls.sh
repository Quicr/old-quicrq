#!/bin/sh
#build last picotls master (for Travis)

# Build at a known-good commit
COMMIT_ID=e64fbfc7f2f87d232e1af38b7aa733a0a412fc71

cd contrib/picotls
git checkout -q "$COMMIT_ID"
cd ../..
