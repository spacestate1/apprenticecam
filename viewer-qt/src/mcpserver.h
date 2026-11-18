#pragma once
// mcpserver - OURS (tooling, not Mastercam behaviour): a local control socket so an MCP bridge
// (tools/mcp_apprenticecam.py) can drive the running app with the MCAMUI_SCRIPT steps of testhooks.cpp.
// Enabled only by `apprenticecam --mcp` or APPRENTICECAM_MCP=1. Protocol: tools/tests/README.md "MCP".
#include <QStringList>
class MainWindow;
namespace McpServer {
// starts the server when --mcp is among the arguments or APPRENTICECAM_MCP=1; true when it listens
bool maybeStart(MainWindow *w, const QStringList &args);
}
