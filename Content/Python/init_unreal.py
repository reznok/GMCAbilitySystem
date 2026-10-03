"""Auto-run at editor startup (PythonScriptPlugin scans each enabled plugin's
Content/Python for init_unreal.py). Registers the GMAS toolset with the
ToolsetRegistry so the unreal-mcp server exposes it.
"""

import gmas_toolset

gmas_toolset.register_toolsets()
