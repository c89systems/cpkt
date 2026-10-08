file(READ "${CPKT_REPO}/.github/workflows/darwin-bundle.yml" workflow)
foreach(required "contents: read" "persist-credentials: false" "ref: \${{ github.sha }}"
    "make test-darwin-native" "test \"$(git rev-parse HEAD)\" = \"\${GITHUB_SHA}\""
    "build/package-stage/arm64-apple-darwin/core/archives/*.tar.gz"
    "if: always()")
  string(FIND "${workflow}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Missing native workflow requirement: ${required}")
  endif()
endforeach()
foreach(forbidden "contents: write" "secrets." "CPKT_HANDOFF" "gh release" "git push" "make release" "python3 tests/")
  string(FIND "${workflow}" "${forbidden}" found)
  if(NOT found LESS 0)
    message(FATAL_ERROR "Unexpected workflow action: ${forbidden}")
  endif()
endforeach()
