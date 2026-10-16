/** Componentes visuais reutilizados pelas telas. */

import { Ionicons } from '@expo/vector-icons';
import { ReactNode } from 'react';
import { ActivityIndicator, Pressable, Text, View } from 'react-native';
import { CatColorKey } from '../domain/cats';
import { PillTone, catColor, pillBox, pillIconColor, pillText, screen } from './tokens';

export function Card({
  children,
  className = '',
}: {
  children: ReactNode;
  className?: string;
}) {
  return (
    <View
      className={`rounded-2xl border border-slate-200 bg-white p-4 dark:border-slate-800 dark:bg-slate-900 ${className}`}
    >
      {children}
    </View>
  );
}

export function SectionTitle({
  children,
  hint,
  className = '',
}: {
  children: ReactNode;
  hint?: string;
  /** Use "flex-1 pr-2" quando o título divide a linha com uma StatusPill. */
  className?: string;
}) {
  return (
    <View className={`gap-0.5 ${className}`}>
      <Text className="text-lg font-semibold text-slate-900 dark:text-slate-50">{children}</Text>
      {hint ? <Text className={screen.hint}>{hint}</Text> : null}
    </View>
  );
}

export function StatusPill({
  label,
  tone = 'neutral',
  icon,
}: {
  label: string;
  tone?: PillTone;
  icon?: keyof typeof Ionicons.glyphMap;
}) {
  return (
    <View
      className={`shrink-0 flex-row items-center gap-1 self-start rounded-full px-3 py-1 ${pillBox[tone]}`}
    >
      {icon ? <Ionicons name={icon} size={14} color={pillIconColor[tone]} /> : null}
      <Text numberOfLines={1} className={`text-xs font-semibold ${pillText[tone]}`}>
        {label}
      </Text>
    </View>
  );
}

type ButtonVariant = 'primary' | 'secondary' | 'danger' | 'ghost';

const buttonStyle: Record<ButtonVariant, { box: string; text: string }> = {
  primary: { box: 'bg-marca-500 active:bg-marca-600', text: 'text-white' },
  secondary: {
    box: 'bg-slate-200 active:bg-slate-300 dark:bg-slate-800 dark:active:bg-slate-700',
    text: 'text-slate-900 dark:text-slate-50',
  },
  danger: { box: 'bg-rose-500 active:bg-rose-600', text: 'text-white' },
  ghost: {
    box: 'border border-slate-300 active:bg-slate-100 dark:border-slate-700 dark:active:bg-slate-800',
    text: 'text-slate-800 dark:text-slate-100',
  },
};

export function BigButton({
  title,
  onPress,
  variant = 'primary',
  icon,
  loading = false,
  disabled = false,
  className = '',
}: {
  title: string;
  onPress: () => void;
  variant?: ButtonVariant;
  icon?: keyof typeof Ionicons.glyphMap;
  loading?: boolean;
  disabled?: boolean;
  className?: string;
}) {
  const style = buttonStyle[variant];
  const inativo = disabled || loading;
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityState={{ disabled: inativo, busy: loading }}
      disabled={inativo}
      onPress={onPress}
      className={`min-h-[52px] flex-row items-center justify-center gap-2 rounded-xl px-4 ${style.box} ${
        inativo ? 'opacity-50' : ''
      } ${className}`}
    >
      {loading ? (
        <ActivityIndicator size="small" color="#fff" />
      ) : icon ? (
        <Ionicons name={icon} size={20} color={variant === 'primary' || variant === 'danger' ? '#fff' : undefined} />
      ) : null}
      <Text className={`text-base font-semibold ${style.text}`}>{title}</Text>
    </Pressable>
  );
}

export function CatAvatar({
  emoji,
  color,
  size = 'md',
}: {
  emoji: string;
  color: CatColorKey;
  size?: 'sm' | 'md' | 'lg';
}) {
  const box = size === 'lg' ? 'h-16 w-16' : size === 'sm' ? 'h-9 w-9' : 'h-12 w-12';
  const texto = size === 'lg' ? 'text-3xl' : size === 'sm' ? 'text-lg' : 'text-2xl';
  return (
    <View className={`items-center justify-center rounded-full ${box} ${catColor[color].soft}`}>
      <Text className={texto}>{emoji}</Text>
    </View>
  );
}

export function WeightDisplay({ text, muted = false }: { text: string; muted?: boolean }) {
  return (
    <Text
      className={`text-4xl font-bold tabular-nums ${
        muted ? 'text-slate-400 dark:text-slate-600' : 'text-slate-900 dark:text-slate-50'
      }`}
    >
      {text}
    </Text>
  );
}

export function EmptyState({
  icon,
  title,
  description,
}: {
  icon: keyof typeof Ionicons.glyphMap;
  title: string;
  description?: string;
}) {
  return (
    <View className="items-center gap-2 px-6 py-10">
      <Ionicons name={icon} size={40} color="#94a3b8" />
      <Text className="text-center text-base font-semibold text-slate-700 dark:text-slate-200">
        {title}
      </Text>
      {description ? <Text className="text-center text-sm text-slate-500">{description}</Text> : null}
    </View>
  );
}

export function Row({ children, className = '' }: { children: ReactNode; className?: string }) {
  return <View className={`flex-row items-center gap-3 ${className}`}>{children}</View>;
}

export function KeyValue({ label, value }: { label: string; value: string }) {
  return (
    <View className="flex-row items-center justify-between py-1">
      <Text className={screen.label}>{label}</Text>
      <Text className="text-sm font-semibold text-slate-900 dark:text-slate-50">{value}</Text>
    </View>
  );
}
