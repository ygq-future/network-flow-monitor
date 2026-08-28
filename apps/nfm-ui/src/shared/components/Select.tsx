export interface SelectOption<T extends string = string> {
  value: T
  label: string
}

export interface SelectProps<T extends string = string> {
  value: T
  onChange: (value: T) => void
  options: SelectOption<T>[]
  className?: string
}

export function Select<T extends string = string>({ value, onChange, options, className = '' }: SelectProps<T>) {
  return (
    <select
      value={value}
      onChange={e => onChange(e.target.value as T)}
      className={`h-8 rounded-lg border border-slate-300 bg-white/90 px-2.5 text-xs font-medium text-slate-800 transition-colors focus:border-blue-500 focus:outline-none dark:border-slate-700 dark:bg-slate-800/90 dark:text-slate-200 ${className}`}>
      {options.map(opt => (
        <option key={opt.value} value={opt.value}>
          {opt.label}
        </option>
      ))}
    </select>
  )
}
