# PenguCat

bongo cat for linux, with a counter that actually works

## what you need

**arch**: `sudo pacman -S base-devel wayland gtk4 gtk4-layer-shell python python-evdev python-gobject`

**ubuntu**: `sudo apt install build-essential libwayland-dev libgtk-4-dev gir1.2-gtk4layer-1.0 python3 python3-evdev python3-gi`

**fedora**: `sudo dnf install gcc make libwayland-client wayland-devel gtk4-devel gtk4-layer-shell-devel python3 python3-evdev python3-gobject`

## build it

```
git clone https://github.com/XLRC888/pengucat.git
cd pengucat
make -j$(nproc)
```

## set it up

```
mkdir -p ~/.config/bongocat
cp bongocat.conf.example ~/.config/bongocat/bongocat.conf
```

## run it

```
./build/bongocat
```

watch for config changes:

```
./build/bongocat --watch-config
```

key counter (gtk overlay that counts your presses):

```
python3 scripts/bongocat-counter
```

toggle everything on/off:

```
./scripts/bongocat-toggle
```

if steam grabs your input and breaks things:

```
./scripts/bongocat-steam
```

## flags

`-h` or `--help` - help
`-v` or `--version` - version
`-c <path>` or `--config <path>` - use a specific config file
`-w` or `--watch-config` - reload config when it changes
`-t` or `--toggle` - toggle on/off
`-m <name>` or `--monitor <name>` - pin to a specific monitor

## where stuff lives

config file: `~/.config/bongocat/bongocat.conf`
counter save: `~/.config/bongocat/keycount`
position: `~/.config/bongocat/position`
