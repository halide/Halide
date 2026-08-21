#!/usr/bin/env bash

set -o pipefail

if [[ -z ${CLANG_TIDY_REAL_BINARY:-} ]]; then
    echo "CLANG_TIDY_REAL_BINARY must be set" 1>&2
    exit 1
fi

if [[ -z ${CLANG_TIDY_ROOT_DIR:-} ]]; then
    echo "CLANG_TIDY_ROOT_DIR must be set" 1>&2
    exit 1
fi

escape_github_annotation_value() {
    local value="$1"
    value="${value//'%'/%25}"
    value="${value//$'\r'/%0D}"
    value="${value//$'\n'/%0A}"
    value="${value//','/%2C}"
    value="${value//':'/%3A}"
    printf '%s' "${value}"
}

OUTPUT_FILE=$(mktemp)
"${CLANG_TIDY_REAL_BINARY}" "$@" >"${OUTPUT_FILE}" 2>&1
CLANG_TIDY_STATUS=$?

grep -v '^[[:digit:]]\+ warnings\? generated\.$' "${OUTPUT_FILE}"

if [[ ${GITHUB_ACTIONS} == "true" ]]; then
    while IFS= read -r line; do
        if [[ ${line} =~ ^(.+):([0-9]+):([0-9]+):[[:space:]](warning|error|note):[[:space:]](.*)\ \[([^][]+)\]$ ]]; then
            file="${BASH_REMATCH[1]}"
            line_no="${BASH_REMATCH[2]}"
            col_no="${BASH_REMATCH[3]}"
            severity="${BASH_REMATCH[4]}"
            message="${BASH_REMATCH[5]}"
            check_name="${BASH_REMATCH[6]}"
        elif [[ ${line} =~ ^(.+):([0-9]+):([0-9]+):[[:space:]](warning|error|note):[[:space:]](.*)$ ]]; then
            file="${BASH_REMATCH[1]}"
            line_no="${BASH_REMATCH[2]}"
            col_no="${BASH_REMATCH[3]}"
            severity="${BASH_REMATCH[4]}"
            message="${BASH_REMATCH[5]}"
            check_name="clang-tidy"
        else
            continue
        fi

        if [[ ${file} == "${CLANG_TIDY_ROOT_DIR}/"* ]]; then
            file="${file#"${CLANG_TIDY_ROOT_DIR}/"}"
        fi

        file="$(escape_github_annotation_value "${file}")"
        title="$(escape_github_annotation_value "${check_name}")"
        message="$(escape_github_annotation_value "${message}")"

        echo "::${severity} file=${file},line=${line_no},col=${col_no},title=${title}::${message}"
    done <"${OUTPUT_FILE}"
fi

rm -f "${OUTPUT_FILE}"
exit "${CLANG_TIDY_STATUS}"
