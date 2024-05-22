fpdunlock_sources = [
  'fpd-unlock.c',
  'main.c',
]

fpdunlock_deps = [
  dependency('glib-2.0'),
  dependency('gio-2.0'),
  dependency('gio-unix-2.0')
]

executable('fpd-unlockd', fpdunlock_sources,
  dependencies: fpdunlock_deps,
  install_dir: bindir,
  install: true,
)