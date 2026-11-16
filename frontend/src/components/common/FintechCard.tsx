import React from 'react';

interface FintechCardProps {
  title?: string;
  subtitle?: string;
  icon?: React.ReactNode;
  action?: React.ReactNode;
  children: React.ReactNode;
  className?: string;
  glow?: boolean;
}

export const FintechCard: React.FC<FintechCardProps> = ({
  title,
  subtitle,
  icon,
  action,
  children,
  className = '',
  glow = false,
}) => {
  return (
    <div
      className={`bg-fintech-card/90 backdrop-blur-md border border-fintech-borderSubtle rounded-2xl p-5 shadow-fintech-card transition-all duration-200 ${
        glow ? 'border-emerald-500/30 shadow-fintech-glow' : 'hover:border-fintech-border'
      } ${className}`}
    >
      {(title || action || icon) && (
        <div className="flex items-center justify-between gap-4 mb-4 pb-3 border-b border-fintech-borderSubtle">
          <div className="flex items-center gap-3">
            {icon && <div className="p-2 bg-fintech-surface rounded-xl text-emerald-400 border border-fintech-border">{icon}</div>}
            <div>
              {title && <h3 className="font-semibold text-base text-slate-100 tracking-tight">{title}</h3>}
              {subtitle && <p className="text-xs text-slate-400 mt-0.5">{subtitle}</p>}
            </div>
          </div>
          {action && <div>{action}</div>}
        </div>
      )}
      <div>{children}</div>
    </div>
  );
};
