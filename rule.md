# Collaboration Rules

## Requesting UI changes (`ui/`)

Describe what you want in plain language — the same way backend changes are requested.

### Feature requests
> "Add a loading spinner to the shipment list while data is fetching"
> "Show an error toast when the API returns 4xx or 5xx"

### Layout / style changes
> "Move the logout button to the top-right corner of the navbar"
> "Make the shipment table sortable by AWB number and date"

### API wiring
> "The login form should POST to `/api/v1/account/login` and store the token in localStorage"

### Bug fixes
> "The date picker on the shipment form clears when I tab away — fix it"

### Working from what you show
- **Screenshot**: share the file path — Claude will read it visually
- **Browser console error**: paste it and Claude will trace it to the source
- **Component / service name**: name the file and describe the problem

### Limitation
Claude cannot run the Angular dev server and see the rendered UI.
For visual/layout work the change will be made and you will be told exactly
where to verify it in the browser — Claude will not claim visual verification
it cannot perform.

---

## Requesting backend changes (`backend/`)

- Name the function, method, or file, or describe the behaviour to change.
- Paste compiler errors or runtime logs directly — Claude will diagnose them.
- For refactoring, say "refactor X" and Claude will propose the approach before touching code.

---

## Requesting Docker / infrastructure changes

- Paste the exact error output from `docker compose up` or `docker logs`.
- Specify which service (`mongodb`, `app`) if known.
- Claude will not run `docker compose down -v` (destructive) without explicit confirmation.

---

## General rules

- Claude commits only when explicitly asked ("commit", "commit & push").
- Destructive operations (`down -v`, `reset --hard`, `branch -D`) require explicit confirmation.
- One concern per request keeps changes reviewable; batching is fine when items are related.
