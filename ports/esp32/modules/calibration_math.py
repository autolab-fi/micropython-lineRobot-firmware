def summarize_straight_passes(passes):
    """Return encoder-balance metrics without making a tuning decision."""
    if not passes:
        raise ValueError("at least one calibration pass is required")

    total_left = 0.0
    total_right = 0.0
    normalized = []
    for left, right in passes:
        left = abs(float(left))
        right = abs(float(right))
        if left <= 0.01 or right <= 0.01:
            raise ValueError("encoder progress too small")
        total_left += left
        total_right += right
        normalized.append({"left_rad": left, "right_rad": right})

    avg_left = total_left / len(normalized)
    avg_right = total_right / len(normalized)
    mean_progress = (avg_left + avg_right) / 2.0
    signed_mismatch = (avg_left - avg_right) / mean_progress

    return {
        "passes": normalized,
        "avg_left_rad": avg_left,
        "avg_right_rad": avg_right,
        "signed_mismatch_ratio": signed_mismatch,
        "mismatch_ratio": abs(signed_mismatch),
    }
