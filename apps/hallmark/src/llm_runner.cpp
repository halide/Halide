#include "llm.h"

#include <HalideRuntime.h>

#include <chrono>
#include <iomanip>
#include <iostream>

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

ABSL_FLAG(bool, show_timing, false,
          "Show timing for operations.");

namespace {

// Prefer high_resolution_clock, but only if it's steady...
template<bool HighResIsSteady = std::chrono::high_resolution_clock::is_steady>
struct SteadyClock {
    using type = std::chrono::high_resolution_clock;
};

// ...otherwise use steady_clock.
template<>
struct SteadyClock<false> {
    using type = std::chrono::steady_clock;
};


struct TimingScope {
  TimingScope(const char *name, int iterations = 1) : name(name), iterations(iterations) {
        start = SteadyClock<>::type::now();
    }

    ~TimingScope() {
        if (absl::GetFlag(FLAGS_show_timing)) {
            SteadyClock<>::type::time_point end = SteadyClock<>::type::now();
            double secs = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
            std::cerr << name << ": took " << secs << "s";
            if (iterations != 1) {
                std::cerr << " " << secs / iterations << "s per iteration.\n";
            } else {
                std::cerr << "\n";
            }
        }
    }

    std::string name;
    int iterations;
    SteadyClock<>::type::time_point start;
};

}

int main(int argc, char *argv[]) {
    absl::ParseCommandLine(argc, argv);

    auto model_path = absl::GetFlag(FLAGS_model_path);
    auto tokenizer_path = absl::GetFlag(FLAGS_tokenizer_path);
    auto prompt = absl::GetFlag(FLAGS_prompt);
    auto max_tokens = absl::GetFlag(FLAGS_max_tokens);

    sentencepiece::SentencePieceProcessor tokenizer;
    {
        TimingScope load_tokenizer("Loading tokenizer");
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

    hallmark::LlmParams llm_params;
    {
        TimingScope load_tokenizer("Loading LLM params");
        auto p = hallmark::LoadLlmParams(model_path);
        if (!p.ok()) {
            std::cerr << p.status() << "\n";
            return 1;
        }
        llm_params = std::move(p.value());
    }
    llm_params.seq_size_T = max_tokens;

    hallmark::LlmWeights llm_weights;
    {
        TimingScope load_tokenizer("Loading LLM params");
        auto w = hallmark::LoadLlmWeights(model_path, llm_params);
        if (!w.ok()) {
            std::cerr << w.status() << "\n";
            return 1;
        }
        llm_weights = std::move(w.value());
    }

    std::unique_ptr<hallmark::Llm> llm;
    {
        TimingScope load_tokenizer("Creating LLM");
        auto l = hallmark::Llm::CreateLlm(llm_weights, llm_params);
        if (!l.ok()) {
            std::cerr << l.status() << "\n";
            return 2;
        }
        llm = std::move(l.value());
    }

    if (!llm->Reset().ok()) {
        std::cerr << "Reset fails\n";
        return 3;
    }
    {
        TimingScope load_tokenizer("Init attention mask");
        if (!llm->InitAttentionMaskValues(llm_params.seq_size_T).ok()) {
            std::cerr << "InitAttentionMaskValues fails\n";
            return 4;
        }
    }

    {
        TimingScope load_tokenizer("Init input tokens", prompt_tokens.size());
        if (!llm->InitInputTokens(prompt_tokens).ok()) {
            std::cerr << "InitInputTokens fails\n";
            return 1;
        }
    }

    std::cout << prompt << "\n";

    {
        TimingScope generate("\nGenerate tokens", max_tokens);
        std::vector<int> output_tokens;
        for (int token = prompt_tokens.size(); token < max_tokens - 2; token += output_tokens.size()) {
          output_tokens.clear();
            if (!llm->GetNextToken(&output_tokens).ok()) {
                std::cerr << "GetNextToken fails\n";
                return 6;
            }
            if (output_tokens.empty()) {
                std::cerr << "Empty result from GetNextToken.\n";
            } else if (output_tokens.size() > 1) {
                std::cerr << "More than one token returned from GetNextToken token " << token << ".\n";
            }
            std::string decoded_tokens;
            if (!tokenizer.Decode(output_tokens, &decoded_tokens).ok()) {
                std::cerr << "Decode fails\n";
                return 7;
            }
            if (decoded_tokens.empty()) {
                std::cout << "_";
            }
            std::cout << decoded_tokens;
            std::cout.flush();
        }
    }

    return 0;
}
