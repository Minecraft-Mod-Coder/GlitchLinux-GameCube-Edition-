# GlitchLinux-GameCube-Edition-
All Code is Used under the License.
RESPECT THE LICENSE! THANKS!

## GlitchLinux Workbench prototype

`glitchlinux.sh` is a small, shell-based desktop launcher prototype. Run it on
an existing Linux system with `sh glitchlinux.sh`. It offers a shell, terminal
or system-browser web access (`lynx`, `w3m`, `links`, or `xdg-open`), ALSA
volume controls (`amixer`), and repository details and releases from GitHub's
public REST API (`curl` and `jq`). Settings can also list Linux keyboard and
mouse devices. Set
`GLITCHLINUX_REPOSITORY=owner/name` to point the GitHub check at another repo.
When `dialog` is installed, its menus accept mouse and keyboard input in
compatible terminals; otherwise, the text menus use the keyboard.

## Native GameCube homebrew

`gc_homebrew/` contains a native libogc application and a devkitPro build
target. Install the GameCube development packages (devkitPPC and libogc), set
`DEVKITPRO` and `DEVKITPPC`, then build with:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC="$DEVKITPRO/devkitPPC"
make -C gc_homebrew
```

Alternatively, build in the devkitPro container without installing the SDK on
the host:

```sh
docker run --rm --user "$(id -u):$(id -g)" \
	-v "$PWD:/project" -w /project \
	-e DEVKITPRO=/opt/devkitpro \
	-e DEVKITPPC=/opt/devkitpro/devkitPPC \
	devkitpro/devkitppc:latest make -C gc_homebrew
```

The `Build GameCube DOL` GitHub Actions workflow installs the SDK on an
`ubuntu-latest` runner and uploads the generated DOL as a workflow artifact.

The output is `gc_homebrew/glitchlinux_gc.dol`, which can be launched by Swiss
or another GameCube homebrew loader. The app has a controller-operated menu,
command palette, video information, and controller rumble test. Its Browser
screen fetches and displays pages from three HTTP bookmarks. Its GitHub screen
has repository, releases, and issues views and attempts requests to the GitHub
REST API. The text browser currently supports HTTP only. GitHub redirects API
requests to HTTPS, and this DOL does not yet include TLS, so live GitHub data
requires a future TLS-enabled transport. Both network apps require a working
GameCube Broadband Adapter connection.

A DOL is a GameCube executable, not a bootable Linux distribution. This native
homebrew runs directly on the console through a loader; `glitchlinux.sh` remains
a separate prototype for existing Linux systems. A true GameCube Linux system
requires a compatible kernel, boot path, and hardware drivers.
