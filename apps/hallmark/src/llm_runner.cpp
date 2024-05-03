#include <HalideRuntime.h>
#include <iostream>
#include "llm.h"

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "src/sentencepiece_processor.h"

ABSL_FLAG(std::optional<std::string>, prompt, std::nullopt,
          "LLM prompt.");

int main(int argc, char *argv[]) {
    absl::ParseCommandLine(argc, argv);
    return 0;
}
