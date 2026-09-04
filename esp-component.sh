#!/bin/bash
set -e

NAMESPACE="h-000-h"
NAME="mini_tree"
VERSION="1.2.0"

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
