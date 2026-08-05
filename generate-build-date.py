from datetime import datetime, timezone
from pathlib import Path

date = datetime.now(timezone.utc).strftime("%a, %d %b %Y %H:%M:%S GMT")

content = f'''#pragma once
#define BUILD_LAST_MODIFIED "{date}"
'''

Path("src/generated").mkdir(exist_ok=True)

with open("src/generated/build_info.hpp", "w") as f:
    f.write(content)