
## Regression matrix

The following supported session and presentation combinations must retain a
working autohide reveal path. For an explicitly selected presentation that
cannot provide a reliable reveal trigger, DockLight must keep autohide safely
disabled instead of leaving the dock unreachable.

| Desktop/session | Presentation setting | Expected presentation | Autohide reveal expectation |
| --- | --- | --- | --- |
| KDE Plasma Wayland | `auto` | Native Wayland/layer-shell | KWin screen-edge reveal must work |
| KDE Plasma Wayland | `native` | Native Wayland/layer-shell | KWin screen-edge reveal must work |
| KDE Plasma Wayland | `xwayland` | XWayland | Reveal must work, or autohide must remain safely disabled |
| KDE Plasma X11 | `auto` | Native X11 | X11 edge trigger must reveal the dock |
| XFCE X11 | `auto` | Native X11 | X11 edge trigger must reveal the dock |
| MATE/Marco X11 | `auto` | Native X11 | X11 edge trigger must reveal the dock |
| MATE/Metacity X11 | `auto` | Native X11 | X11 edge trigger must reveal the dock |
| Cinnamon X11 | `auto` | Native X11 | X11 edge trigger must reveal the dock |
| GNOME Shell X11 | `auto` | Native X11 | X11 edge trigger must work with and without the optional Shell animation bridge |
| GNOME Wayland | `auto` | XWayland | GNOME Shell reveal must work |
| GNOME Wayland | `native` | Native Wayland | GNOME Shell reveal must work |
| GNOME Wayland | `xwayland` | XWayland | GNOME Shell reveal must work |
| GNOME Flashback X11 | `auto` | Native X11 | X11 edge trigger must reveal the dock |
| LXDE/Openbox X11 | `auto` | Native X11 | X11 edge trigger must reveal the dock |
| LXQt/Openbox X11 | `auto` | Native X11 | X11 edge trigger must reveal the dock |
| Other EWMH-compatible X11 desktops | `auto` | Native X11 | Generic X11 edge trigger must reveal the dock |
| Hyprland Wayland | `auto` | XWayland | XWayland edge trigger must reveal the dock |
| Hyprland Wayland | `xwayland` | XWayland | XWayland edge trigger must reveal the dock |
| Hyprland Wayland | `native` | Native Wayland/layer-shell | Layer-shell edge trigger must reveal the dock; the known drag-icon limitation remains |

## Integration tests

`PASS` means confirmed in a real desktop session. `PENDING` means the backend
is implemented but still needs real-session verification. `NOT IMPLEMENTED`
means DockLight can start, but desktop-specific window integration is not
available.

<table>
  <thead>
    <tr>
      <th>Desktops</th>
      <th>Session</th>
      <th>WM</th>
      <th>Compositor</th>
      <th>Selected backend</th>
      <th>Result</th>
    </tr>
  </thead>
  <tbody>
    <tr><td>KDE Plasma</td><td>Wayland</td><td>KWin</td><td>KWin</td><td><code>KWinWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the KWin script and D-Bus integration.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>KDE Plasma</td><td>X11</td><td>KWin</td><td>KWin</td><td><code>KWinX11WindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses native X11/EWMH window integration.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>XFCE</td><td>X11</td><td>xfwm4</td><td>xfwm4</td><td><code>Xfwm4WindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the desktop-specific X11 backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>MATE</td><td>X11</td><td>Marco or Metacity</td><td>Marco or Metacity</td><td><code>MarcoWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Marco and Metacity share this X11 backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Cinnamon</td><td>X11</td><td>Muffin</td><td>Muffin</td><td><code>MuffinWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the desktop-specific X11 backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>GNOME</td><td>X11</td><td>Mutter / GNOME Shell</td><td>Mutter</td><td><code>GnomeX11WindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Keeps native X11/EWMH window integration and XComposite previews; the GNOME Shell extension is restricted to the main dock's GNOME autohide animation. The original native animation remains the fallback.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>GNOME</td><td>Wayland</td><td>Mutter / GNOME Shell</td><td>Mutter</td><td><code>GnomeWaylandWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the GNOME Shell extension bridge.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>GNOME Flashback</td><td>X11</td><td>Metacity or Marco</td><td>Metacity or Marco</td><td><code>MarcoWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the shared Marco/Metacity X11 backend instead of the GNOME Shell extension bridge.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>LXDE</td><td>X11</td><td>Openbox</td><td>compton or picom</td><td><code>OpenboxWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> A compositor is required for complete window previews.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>LXQt</td><td>X11</td><td>Openbox</td><td>compton or picom</td><td><code>OpenboxWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Implemented; real-session verification remains.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Other EWMH desktops</td><td>X11</td><td>Other EWMH WM</td><td>Any or none</td><td><code>EwmhFallbackWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Generic EWMH-compatible fallback.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>XFCE</td><td>Wayland</td><td>N/A</td><td>Varies</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Sway</td><td>Wayland</td><td>N/A</td><td>Sway / wlroots</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Hyprland</td><td>Wayland</td><td>Hyprland</td><td>Hyprland</td><td><code>HyprlandWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Real-session verification uses the default XWayland presentation with native Hyprland JSON IPC for stable window identity, cross-workspace focus, close, maximize, geometry, workspace state, standard Wayland image-copy previews, and compositor-native reserved space when autohide is disabled. Traditional minimize is intentionally unavailable. Explicit native GTK Wayland presentation remains available for testing, but currently has a drag-icon hotspot shift.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Wayfire</td><td>Wayland</td><td>N/A</td><td>Wayfire / wlroots</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>labwc</td><td>Wayland</td><td>N/A</td><td>labwc / wlroots</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>COSMIC</td><td>Wayland</td><td>N/A</td><td>COSMIC compositor</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Generic or other desktop</td><td>Wayland</td><td>N/A</td><td>Other Wayland compositor</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
  </tbody>
</table>