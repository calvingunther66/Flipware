# Flipware website

Static site for [flipware-fw.dev](https://flipware-fw.dev), served by a
Cloudflare Worker with static assets (`wrangler.jsonc`).

- `public/` holds the site. Images in `public/img` come from the firmware's
  brand art (`scripts/flipware_branding.py`).
- Preview locally: `npx wrangler dev`
- Deploy: `npx wrangler deploy`
