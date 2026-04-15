import { useEffect, useMemo, useState } from "react";
import {
  Activity,
  Droplets,
  Thermometer,
  Leaf,
  Power,
  Save,
  Plus,
  Trash2,
  Pencil,
} from "lucide-react";
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  Tooltip,
  ResponsiveContainer,
  CartesianGrid,
  Legend,
} from "recharts";
import { supabase } from "./lib/supabase";

const MODE_OPTIONS = ["threshold", "schedule"];
const DAY_OPTIONS = [
  { label: "CN", value: 1 },
  { label: "T2", value: 2 },
  { label: "T3", value: 3 },
  { label: "T4", value: 4 },
  { label: "T5", value: 5 },
  { label: "T6", value: 6 },
  { label: "T7", value: 7 },
];

const DEFAULT_CONFIG = {
  id: 1,
  dry_threshold: 900,
  wet_threshold: 600,
  safe_temp: 32,
  timeout_ms: 600000,
  pump_mode: "threshold",
  pump_status: 0,
};

const DEFAULT_SCHEDULE_FORM = {
  start_time: "06:00:00",
  duration_minutes: 10,
  days_of_week: [2, 4, 6],
  enabled: true,
};

function toFloat(value) {
  if (value == null) return 0;
  const parsed = Number(value);
  return Number.isNaN(parsed) ? 0 : parsed;
}

function normalizePumpStatus(status) {
  if (status === 1 || status === "1" || status === true) return "PUMPING";
  if (status === 0 || status === "0" || status === false || status == null)
    return "IDLE";

  const normalized = String(status).toUpperCase();
  if (normalized === "ON") return "PUMPING";
  if (normalized === "OFF") return "IDLE";
  return normalized;
}

function statusClasses(status) {
  const base =
    "inline-flex items-center rounded-full px-3 py-1 text-xs font-semibold";
  switch (status) {
    case "PUMPING":
      return `${base} bg-emerald-500/20 text-emerald-300 ring-1 ring-emerald-500/40 animate-pulse`;
    case "OVERHEAT":
      return `${base} bg-amber-500/20 text-amber-300 ring-1 ring-amber-500/40`;
    case "ERROR":
      return `${base} bg-rose-500/20 text-rose-300 ring-1 ring-rose-500/40`;
    default:
      return `${base} bg-slate-500/20 text-slate-200 ring-1 ring-slate-500/40`;
  }
}

function formatTimeLabel(raw) {
  if (!raw) return "--:--";
  const date = new Date(raw);
  if (Number.isNaN(date.getTime())) return String(raw).slice(11, 16);
  return date.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

function App() {
  const [telemetry, setTelemetry] = useState([]);
  const [config, setConfig] = useState(DEFAULT_CONFIG);
  const [configForm, setConfigForm] = useState(DEFAULT_CONFIG);
  const [schedules, setSchedules] = useState([]);
  const [scheduleForm, setScheduleForm] = useState(DEFAULT_SCHEDULE_FORM);
  const [editingId, setEditingId] = useState(null);
  const [busy, setBusy] = useState(false);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");

  const latestTelemetry = telemetry[telemetry.length - 1];
  const currentPumpStatus = normalizePumpStatus(
    config.pump_status ?? latestTelemetry?.pump_status,
  );

  const chartData = useMemo(
    () =>
      telemetry.map((item) => ({
        time: formatTimeLabel(item.created_at),
        temp: toFloat(item.temp),
        hum: toFloat(item.hum),
        soil: toFloat(item.soil),
      })),
    [telemetry],
  );

  const stats = [
    {
      title: "Nhiệt độ",
      value: `${toFloat(latestTelemetry?.temp).toFixed(1)}°C`,
      icon: Thermometer,
      color: "text-orange-300",
    },
    {
      title: "Độ ẩm không khí",
      value: `${toFloat(latestTelemetry?.hum).toFixed(1)}%`,
      icon: Droplets,
      color: "text-sky-300",
    },
    {
      title: "Độ ẩm đất",
      value: `${toFloat(latestTelemetry?.soil).toFixed(1)}%`,
      icon: Leaf,
      color: "text-lime-300",
    },
  ];

  useEffect(() => {
    const bootstrap = async () => {
      setLoading(true);
      setError("");

      const [
        { data: telemetryRows, error: telemetryError },
        { data: configRow, error: configError },
        { data: scheduleRows, error: scheduleError },
      ] = await Promise.all([
        supabase
          .from("telemetry")
          .select("*")
          .order("created_at", { ascending: false })
          .limit(20),
        supabase.from("config").select("*").eq("id", 1).limit(1).maybeSingle(),
        supabase
          .from("schedules")
          .select("*")
          .order("start_time", { ascending: true }),
      ]);

      if (telemetryError || configError || scheduleError) {
        setError(
          telemetryError?.message ||
            configError?.message ||
            scheduleError?.message ||
            "Không thể tải dữ liệu",
        );
      }

      if (telemetryRows) {
        setTelemetry([...telemetryRows].reverse());
      }

      if (configRow) {
        const merged = {
          ...DEFAULT_CONFIG,
          ...configRow,
          pump_status: normalizePumpStatus(configRow.pump_status),
        };
        setConfig(merged);
        setConfigForm(merged);
      } else if (!configError) {
        const { error: seedConfigError } = await supabase.from("config").upsert(
          {
            id: 1,
            dry_threshold: DEFAULT_CONFIG.dry_threshold,
            wet_threshold: DEFAULT_CONFIG.wet_threshold,
            safe_temp: DEFAULT_CONFIG.safe_temp,
            timeout_ms: DEFAULT_CONFIG.timeout_ms,
            pump_mode: DEFAULT_CONFIG.pump_mode,
          },
          { onConflict: "id" },
        );

        if (seedConfigError) {
          setError(seedConfigError.message);
        } else {
          setConfig(DEFAULT_CONFIG);
          setConfigForm(DEFAULT_CONFIG);
        }
      }

      if (scheduleRows) {
        setSchedules(scheduleRows);
      }

      setLoading(false);
    };

    bootstrap();

    const telemetryChannel = supabase
      .channel("telemetry-live")
      .on(
        "postgres_changes",
        { event: "INSERT", schema: "public", table: "telemetry" },
        (payload) => {
          setTelemetry((prev) => {
            const next = [...prev, payload.new];
            return next.slice(-20);
          });
        },
      )
      .subscribe();

    const configChannel = supabase
      .channel("config-live")
      .on(
        "postgres_changes",
        { event: "*", schema: "public", table: "config", filter: "id=eq.1" },
        (payload) => {
          const next = {
            ...DEFAULT_CONFIG,
            ...payload.new,
            pump_status: normalizePumpStatus(payload.new?.pump_status),
          };
          setConfig(next);
          setConfigForm(next);
        },
      )
      .subscribe();

    const schedulesChannel = supabase
      .channel("schedules-live")
      .on(
        "postgres_changes",
        { event: "*", schema: "public", table: "schedules" },
        async () => {
          const { data } = await supabase
            .from("schedules")
            .select("*")
            .order("start_time", { ascending: true });
          if (data) setSchedules(data);
        },
      )
      .subscribe();

    return () => {
      supabase.removeChannel(telemetryChannel);
      supabase.removeChannel(configChannel);
      supabase.removeChannel(schedulesChannel);
    };
  }, []);

  const onModeChange = async (mode) => {
    setBusy(true);
    setError("");
    const { error: updateError } = await supabase
      .from("config")
      .upsert({ id: 1, pump_mode: mode }, { onConflict: "id" });
    if (updateError) setError(updateError.message);
    setBusy(false);
  };

  const onConfigFieldChange = (name, value) => {
    setConfigForm((prev) => ({ ...prev, [name]: value }));
  };

  const onSaveConfig = async (event) => {
    event.preventDefault();
    setBusy(true);
    setError("");

    const payload = {
      dry_threshold: Number(configForm.dry_threshold),
      wet_threshold: Number(configForm.wet_threshold),
      safe_temp: Number(configForm.safe_temp),
      timeout_ms: Number(configForm.timeout_ms),
    };

    const { error: updateError } = await supabase
      .from("config")
      .upsert({ id: 1, ...payload }, { onConflict: "id" });
    if (updateError) setError(updateError.message);
    setBusy(false);
  };

  const onScheduleFormChange = (name, value) => {
    setScheduleForm((prev) => ({ ...prev, [name]: value }));
  };

  const onToggleDay = (day) => {
    setScheduleForm((prev) => {
      const exists = prev.days_of_week.includes(day);
      const nextDays = exists
        ? prev.days_of_week.filter((d) => d !== day)
        : [...prev.days_of_week, day];
      return { ...prev, days_of_week: nextDays.sort((a, b) => a - b) };
    });
  };

  const resetScheduleForm = () => {
    setScheduleForm(DEFAULT_SCHEDULE_FORM);
    setEditingId(null);
  };

  const onScheduleSubmit = async (event) => {
    event.preventDefault();
    setBusy(true);
    setError("");

    const payload = {
      start_time: scheduleForm.start_time,
      duration_minutes: Number(scheduleForm.duration_minutes),
      days_of_week: scheduleForm.days_of_week,
      enabled: Boolean(scheduleForm.enabled),
    };

    const query = editingId
      ? supabase.from("schedules").update(payload).eq("id", editingId)
      : supabase.from("schedules").insert(payload);

    const { error: scheduleError } = await query;
    if (scheduleError) {
      setError(scheduleError.message);
    } else {
      resetScheduleForm();
    }
    setBusy(false);
  };

  const onEditSchedule = (schedule) => {
    setEditingId(schedule.id);
    setScheduleForm({
      start_time: schedule.start_time,
      duration_minutes: schedule.duration_minutes,
      days_of_week: schedule.days_of_week ?? [],
      enabled: schedule.enabled,
    });
  };

  const onDeleteSchedule = async (id) => {
    setBusy(true);
    setError("");
    const { error: deleteError } = await supabase
      .from("schedules")
      .delete()
      .eq("id", id);
    if (deleteError) setError(deleteError.message);
    if (editingId === id) resetScheduleForm();
    setBusy(false);
  };

  return (
    <main className="mx-auto w-full max-w-6xl p-4 pb-8 sm:p-6">
      <header className="mb-6 rounded-2xl border border-slate-800 bg-slate-900/70 p-4 sm:p-6">
        <h1 className="text-2xl font-bold text-white">IrriFlow Dashboard</h1>
        <p className="mt-2 text-sm text-slate-300">
          Giám sát và điều khiển hệ thống tưới thời gian thực.
        </p>
      </header>

      {error && (
        <div className="mb-4 rounded-xl border border-rose-500/40 bg-rose-500/10 px-4 py-3 text-sm text-rose-200">
          {error}
        </div>
      )}

      <section className="grid grid-cols-1 gap-3 sm:grid-cols-3">
        {stats.map((stat) => {
          const Icon = stat.icon;
          return (
            <article
              key={stat.title}
              className="rounded-2xl border border-slate-800 bg-slate-900/70 p-4"
            >
              <div className="flex items-center justify-between">
                <span className="text-sm text-slate-300">{stat.title}</span>
                <Icon className={`h-5 w-5 ${stat.color}`} />
              </div>
              <p className="mt-4 text-3xl font-bold text-white">
                {loading ? "..." : stat.value}
              </p>
            </article>
          );
        })}
      </section>

      <section className="mt-4 grid grid-cols-1 gap-4 lg:grid-cols-3">
        <article className="rounded-2xl border border-slate-800 bg-slate-900/70 p-4">
          <div className="flex items-center justify-between">
            <h2 className="text-sm font-semibold text-slate-200">
              Pump Status
            </h2>
            <Power className="h-5 w-5 text-slate-300" />
          </div>
          <div className="mt-4 flex items-center gap-3">
            <span className={statusClasses(currentPumpStatus)}>
              {currentPumpStatus}
            </span>
            <span className="text-xs text-slate-400">
              Mode: {config.pump_mode}
            </span>
          </div>
        </article>

        <article className="rounded-2xl border border-slate-800 bg-slate-900/70 p-4 lg:col-span-2">
          <div className="mb-4 flex items-center gap-2">
            <Activity className="h-5 w-5 text-cyan-300" />
            <h2 className="text-sm font-semibold text-slate-200">
              Telemetry (20 điểm gần nhất)
            </h2>
          </div>
          <div className="h-60 w-full">
            <ResponsiveContainer>
              <LineChart data={chartData}>
                <CartesianGrid strokeDasharray="3 3" stroke="#334155" />
                <XAxis dataKey="time" stroke="#94a3b8" fontSize={12} />
                <YAxis stroke="#94a3b8" fontSize={12} />
                <Tooltip
                  contentStyle={{
                    backgroundColor: "#0f172a",
                    borderColor: "#334155",
                    borderRadius: "0.75rem",
                    fontSize: "12px",
                  }}
                />
                <Legend />
                <Line
                  type="monotone"
                  dataKey="temp"
                  name="Nhiệt độ"
                  stroke="#fb923c"
                  strokeWidth={2}
                  dot={false}
                />
                <Line
                  type="monotone"
                  dataKey="hum"
                  name="Ẩm KK"
                  stroke="#38bdf8"
                  strokeWidth={2}
                  dot={false}
                />
                <Line
                  type="monotone"
                  dataKey="soil"
                  name="Ẩm đất"
                  stroke="#a3e635"
                  strokeWidth={2}
                  dot={false}
                />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </article>
      </section>

      <section className="mt-4 grid grid-cols-1 gap-4 lg:grid-cols-2">
        <article className="rounded-2xl border border-slate-800 bg-slate-900/70 p-4">
          <h2 className="text-sm font-semibold text-slate-200">
            Control Panel
          </h2>

          <div className="mt-4">
            <p className="mb-2 text-xs text-slate-400">Mode Switch</p>
            <div className="grid grid-cols-2 gap-2">
              {MODE_OPTIONS.map((mode) => (
                <button
                  key={mode}
                  type="button"
                  onClick={() => onModeChange(mode)}
                  disabled={busy}
                  className={`rounded-lg px-3 py-2 text-sm font-medium transition ${
                    config.pump_mode === mode
                      ? "bg-cyan-500 text-slate-950"
                      : "bg-slate-800 text-slate-200 hover:bg-slate-700"
                  }`}
                >
                  {mode}
                </button>
              ))}
            </div>
          </div>

          <form className="mt-4 grid grid-cols-2 gap-3" onSubmit={onSaveConfig}>
            <label className="text-xs text-slate-400">
              Dry threshold
              <input
                type="number"
                min="0"
                max="1023"
                value={configForm.dry_threshold}
                onChange={(e) =>
                  onConfigFieldChange("dry_threshold", e.target.value)
                }
                className="mt-1 w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 outline-none focus:border-cyan-400"
              />
            </label>
            <label className="text-xs text-slate-400">
              Wet threshold
              <input
                type="number"
                min="0"
                max="1023"
                value={configForm.wet_threshold}
                onChange={(e) =>
                  onConfigFieldChange("wet_threshold", e.target.value)
                }
                className="mt-1 w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 outline-none focus:border-cyan-400"
              />
            </label>
            <label className="text-xs text-slate-400">
              Safe temp (°C)
              <input
                type="number"
                min="0"
                max="100"
                value={configForm.safe_temp}
                onChange={(e) =>
                  onConfigFieldChange("safe_temp", e.target.value)
                }
                className="mt-1 w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 outline-none focus:border-cyan-400"
              />
            </label>
            <label className="text-xs text-slate-400">
              Timeout (ms)
              <input
                type="number"
                min="1000"
                step="500"
                value={configForm.timeout_ms}
                onChange={(e) =>
                  onConfigFieldChange("timeout_ms", e.target.value)
                }
                className="mt-1 w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 outline-none focus:border-cyan-400"
              />
            </label>
            <button
              type="submit"
              disabled={busy}
              className="col-span-2 mt-1 inline-flex items-center justify-center gap-2 rounded-lg bg-cyan-500 px-4 py-2 text-sm font-semibold text-slate-950 transition hover:bg-cyan-400 disabled:cursor-not-allowed disabled:opacity-60"
            >
              <Save className="h-4 w-4" /> Lưu cấu hình
            </button>
          </form>
        </article>

        <article className="rounded-2xl border border-slate-800 bg-slate-900/70 p-4">
          <h2 className="text-sm font-semibold text-slate-200">
            Schedule Manager
          </h2>

          <form className="mt-4 space-y-3" onSubmit={onScheduleSubmit}>
            <div className="grid grid-cols-2 gap-3">
              <label className="text-xs text-slate-400">
                Start time
                <input
                  type="time"
                  step="1"
                  value={String(scheduleForm.start_time).slice(0, 8)}
                  onChange={(e) =>
                    onScheduleFormChange(
                      "start_time",
                      `${e.target.value}:00`.slice(0, 8),
                    )
                  }
                  className="mt-1 w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 outline-none focus:border-cyan-400"
                />
              </label>
              <label className="text-xs text-slate-400">
                Duration (phút)
                <input
                  type="number"
                  min="1"
                  value={scheduleForm.duration_minutes}
                  onChange={(e) =>
                    onScheduleFormChange("duration_minutes", e.target.value)
                  }
                  className="mt-1 w-full rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 outline-none focus:border-cyan-400"
                />
              </label>
            </div>

            <div>
              <p className="mb-2 text-xs text-slate-400">Days of week</p>
              <div className="grid grid-cols-7 gap-1">
                {DAY_OPTIONS.map((day) => {
                  const selected = scheduleForm.days_of_week.includes(
                    day.value,
                  );
                  return (
                    <button
                      key={day.value}
                      type="button"
                      onClick={() => onToggleDay(day.value)}
                      className={`rounded-md px-1 py-1.5 text-xs font-semibold transition ${
                        selected
                          ? "bg-cyan-500 text-slate-950"
                          : "bg-slate-800 text-slate-300"
                      }`}
                    >
                      {day.label}
                    </button>
                  );
                })}
              </div>
            </div>

            <label className="inline-flex items-center gap-2 text-sm text-slate-300">
              <input
                type="checkbox"
                checked={scheduleForm.enabled}
                onChange={(e) =>
                  onScheduleFormChange("enabled", e.target.checked)
                }
                className="h-4 w-4 rounded border-slate-700 bg-slate-900"
              />
              Enable schedule
            </label>

            <div className="flex gap-2">
              <button
                type="submit"
                disabled={busy}
                className="inline-flex flex-1 items-center justify-center gap-2 rounded-lg bg-cyan-500 px-4 py-2 text-sm font-semibold text-slate-950 transition hover:bg-cyan-400 disabled:opacity-60"
              >
                {editingId ? (
                  <Pencil className="h-4 w-4" />
                ) : (
                  <Plus className="h-4 w-4" />
                )}
                {editingId ? "Cập nhật lịch" : "Thêm lịch"}
              </button>
              {editingId && (
                <button
                  type="button"
                  onClick={resetScheduleForm}
                  className="rounded-lg bg-slate-700 px-4 py-2 text-sm font-semibold text-slate-100 hover:bg-slate-600"
                >
                  Hủy
                </button>
              )}
            </div>
          </form>

          <div className="mt-4 space-y-2">
            {schedules.length === 0 && (
              <p className="text-xs text-slate-500">Chưa có lịch nào.</p>
            )}
            {schedules.map((schedule) => (
              <div
                key={schedule.id}
                className="rounded-xl border border-slate-800 bg-slate-950/60 p-3"
              >
                <div className="flex items-center justify-between">
                  <p className="text-sm font-semibold text-white">
                    {String(schedule.start_time).slice(0, 5)} ·{" "}
                    {schedule.duration_minutes} phút
                  </p>
                  <span
                    className={`rounded-full px-2 py-0.5 text-[11px] font-semibold ${
                      schedule.enabled
                        ? "bg-emerald-500/20 text-emerald-300"
                        : "bg-slate-700 text-slate-300"
                    }`}
                  >
                    {schedule.enabled ? "ON" : "OFF"}
                  </span>
                </div>
                <p className="mt-1 text-xs text-slate-400">
                  Ngày: {(schedule.days_of_week ?? []).join(", ") || "-"}
                </p>
                <div className="mt-3 flex gap-2">
                  <button
                    type="button"
                    onClick={() => onEditSchedule(schedule)}
                    className="inline-flex items-center gap-1 rounded-md bg-slate-800 px-2 py-1 text-xs text-slate-200 hover:bg-slate-700"
                  >
                    <Pencil className="h-3.5 w-3.5" /> Sửa
                  </button>
                  <button
                    type="button"
                    onClick={() => onDeleteSchedule(schedule.id)}
                    className="inline-flex items-center gap-1 rounded-md bg-rose-600/80 px-2 py-1 text-xs text-rose-50 hover:bg-rose-600"
                  >
                    <Trash2 className="h-3.5 w-3.5" /> Xóa
                  </button>
                </div>
              </div>
            ))}
          </div>
        </article>
      </section>
    </main>
  );
}

export default App;
