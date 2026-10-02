from gepa_scheduling.copilot import parse_token_usage


def test_parse_token_usage_from_json_lines() -> None:
    output = "\n".join(
        [
            '{"type":"usage","usage":{"input_tokens":12,"output_tokens":4}}',
            '{"type":"usage","usage":{"prompt_tokens":3,"completion_tokens":2}}',
        ]
    )
    assert parse_token_usage(output) == (15, 6)


def test_parse_token_usage_ignores_plain_text() -> None:
    assert parse_token_usage("not json") == (None, None)
