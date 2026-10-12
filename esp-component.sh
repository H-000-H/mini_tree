#!/bin/bash
set -e

cd "$(dirname "$0")"

NAMESPACE="h-000-h"
NAME="mini-tree"
MANIFEST="idf_component.yml"

VERSION="$(awk '/^version:/ { gsub(/["\047]/, "", $2); print $2; exit }' "${MANIFEST}")"
if [ -z "${VERSION}" ]; then
    echo "ERROR: cannot parse version from ${MANIFEST}"
    exit 1
fi

if [ -z "${IDF_COMPONENT_API_TOKEN}" ]; then
    echo "ERROR: IDF_COMPONENT_API_TOKEN environment variable is not set. Please set it to your API token."
    exit 1
fi

echo "uploading ${NAME} version ${VERSION} to esp-component registry"

IDF_COMPONENT_API_TOKEN="${IDF_COMPONENT_API_TOKEN}" \
python3 -m idf_component_manager component upload \
    --namespace "${NAMESPACE}" \
    --name "${NAME}"

echo "pushing ${NAME} version ${VERSION} successfully to esp-component registry"
