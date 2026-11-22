"""Install the pinned page guard for project FreeInkUI consumers."""
import hashlib
from pathlib import Path

Import("env")

# Action bands only consume height; the first empty check already admitted width.
_PAGE = "libs/ui/FreeInkUI/include/components/media/publication-page.h"
_PAGE_BASE = "abae0d2776bdff43708dbbb7648b8aeae4036cc49c119f9ac83e5b2b137081d7"
_PAGE_FIXED = "ece7046127282eb88f355d2e23cde6c60c441048488ea186fec79a400e8ef621"


def publication_page_output(data):
    if hashlib.sha256(data).hexdigest() == _PAGE_BASE:
        data = data.replace(b"action(props.more, false);\n  if (body.empty())",
                            b"action(props.more, false);\n  if (body.height <= 0)")
    if hashlib.sha256(data).hexdigest() != _PAGE_FIXED:
        raise RuntimeError("Unknown SDK publication page bytes: " + _PAGE)
    return data


target = Path(env.subst("$PROJECT_DIR")) / "freeink-sdk" / _PAGE
original = target.read_bytes()  # A missing required header is a build error.
installed = publication_page_output(original)
if installed != original:
    target.write_bytes(installed)
    if hashlib.sha256(target.read_bytes()).hexdigest() != _PAGE_FIXED:
        raise RuntimeError("SDK publication page postimage mismatch: " + str(target))
