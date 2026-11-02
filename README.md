# True FPS System Plugin

Replicated first-person shooter plugin for Unreal Engine 5, in C++. It uses true first person: one full-body mesh, animated by a single-frame pose per weapon plus procedural IK.

This project's public demo led to my hiring at Nevith Games.

- [Watch the demo](https://www.youtube.com/watch?v=WImIDwkdnes)
- [Read the docs](https://tornelli-henrique.gitbook.io/truefps-plugin-documentation/)
- [See the project page](https://tornellihenrique.github.io/work/true-fps-system/)

## What's inside

- Hitscan and projectile weapons, attachments and melee.
- Recoil with camera recoil and a recovery that respects the player's compensation.
- A C++ animation layer, with configuration in data assets and runtime state in structs.
- Wall offset: the weapon pulls back near walls, with an effect on aiming and firing.
- Server-authoritative state and damage, with the owner simulating actions immediately.
- Bots, online sessions and menus.

## Requirements

Developed on Unreal Engine 5.1.

## Credits

The custom animation nodes come from an open source project, used with the author's permission. The multiplayer base started from Epic's ShooterGame sample.

## License

GPL-3.0. See [LICENSE](LICENSE).
