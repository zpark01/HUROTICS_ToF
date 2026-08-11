"""Pick a font that can render CJK if one is installed, otherwise fall back."""
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm

CANDIDATES = ["Malgun Gothic", "AppleGothic", "NanumGothic",
              "Noto Sans CJK KR", "Noto Sans CJK JP", "Noto Sans KR"]


def setup():
    available = {f.name for f in fm.fontManager.ttflist}
    for name in CANDIDATES:
        if name in available:
            plt.rcParams["font.family"] = name
            plt.rcParams["axes.unicode_minus"] = False
            return True
    plt.rcParams["font.family"] = "DejaVu Sans"
    plt.rcParams["axes.unicode_minus"] = False
    return False


CJK_OK = setup()
