#include <HalideRuntime.h>
#include <iomanip>
#include <iostream>
#include "llm.h"

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "src/sentencepiece_processor.h"

ABSL_FLAG(std::string, model_path, "model.tflite",
          "Path to the tflite model file.");

ABSL_FLAG(std::string, tokenizer_path, "tokenizer.spm",
          "Path to the sentence piece model.");

ABSL_FLAG(std::string, prompt, "Write a memo to myself titled \"Do the dumb things I gotta do.\"",
          "Initial prompt for llm.");

ABSL_FLAG(int, max_tokens, 512,
          "Maximum number of input and output tokens. This value needs to be "
          "at least larger than the number of input tokens.");

int main(int argc, char *argv[]) {
    absl::ParseCommandLine(argc, argv);

    auto model_path = absl::GetFlag(FLAGS_model_path);
    auto tokenizer_path = absl::GetFlag(FLAGS_tokenizer_path);
    auto prompt = absl::GetFlag(FLAGS_prompt);
    auto max_tokens = absl::GetFlag(FLAGS_max_tokens);

    sentencepiece::SentencePieceProcessor tokenizer;
    {
        auto result = tokenizer.Load(tokenizer_path);
        if (!result.ok()) {
          std::cerr << result.message();
          return 1;
        }
    }
    
    std::vector<int> prompt_tokens;
    {
        // TODO: Apparently this is required by the Gemma IT
        // model. Find some documentation on the mechanism and see if
        // there is a better way to handle this or to make it
        // conditional on some info from the model file.
        std::string bracketed_prompt = "<start_of_turn>user\n" + prompt +
                                       "<end_of_turn>\n<start_of_turn>model\n";

        auto result = tokenizer.Encode(bracketed_prompt, &prompt_tokens);
    }

    std::cerr << "Loading LLM params.\n";
    auto p = hallmark::LoadLlmParams(model_path);
    if (!p.ok()) {
      return 1;
    }
    auto llm_params = std::move(p.value());
    llm_params.seq_size_T = max_tokens;

    std::cerr << "Loading LLM weights.\n";
    auto w = hallmark::LoadLlmWeights(model_path, llm_params);
    if (!w.ok()) {
      return 1;
    }
    auto llm_weights = std::move(w.value());

    std::cerr << "Creating LLM.\n";
    auto l = hallmark::Llm::CreateLlm(llm_weights, llm_params);
    if (!l.ok()) {
      return 2;
    }
    auto llm = std::move(l.value());

    if (!llm->Reset().ok()) {
      return 3;
    }
    if (!llm->InitAttentionMaskValues(llm_params.seq_size_T).ok()) {
      return 4;
    }

    if (!llm->InitInputTokens(prompt_tokens).ok()) {
      return 1;
    }

    std::cout << prompt << "\n";

    for (int token = prompt_tokens.size(); token < max_tokens; token++) {
      std::vector<int> output_tokens;
      if (!llm->GetNextToken(&output_tokens).ok()) {
        return 6;
      }
      if (output_tokens.empty()) {
        std::cerr << "Empty result from GetNextToken.\n";
      }
      std::string decoded_tokens;
      if (!tokenizer.Decode(output_tokens, &decoded_tokens).ok()) {
        return 7;
      }
      if (decoded_tokens.empty()) {

        std::cout << "_";
      }
      std::cout << decoded_tokens;
      std::cout.flush();
    }

    return 0;
}
