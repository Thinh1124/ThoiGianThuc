# IrriFlow Frontend

## Start

1. Copy `.env.example` to `.env` and fill Supabase keys.
2. Install dependencies:

```bash
npm install
```

3. Run dev server:

```bash
npm run dev
```

## Features

- Real-time telemetry monitor via Supabase Realtime.
- Config sync and pump mode control (`threshold` and `schedule`).
- Real-time schedule override for pump on/off control.
- Threshold configuration update.
- Schedule CRUD for fixed watering windows.
