"""GMAS (GMC Ability System) authoring toolset for the ToolsetRegistry MCP server.

Ported from the Nwiro plugin's GMAS MCP tools.
"""

import unreal

from gmas_toolset.gmas import GMASToolset

_TOOLSETS = [GMASToolset]


def register_toolsets():
    for toolset in _TOOLSETS:
        unreal.ToolsetRegistry.register_toolset_class(toolset)


def unregister_toolsets():
    for toolset in _TOOLSETS:
        unreal.ToolsetRegistry.unregister_toolset_class(toolset)
