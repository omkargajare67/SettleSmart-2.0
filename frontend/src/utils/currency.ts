export function formatPaiseToINR(paise?: number | string | bigint | null): string {
  if (paise === undefined || paise === null || (typeof paise === 'number' && isNaN(paise))) {
    return '₹0.00';
  }
  const p = typeof paise === 'string' ? BigInt(paise) : BigInt(Math.round(Number(paise)));
  const isNegative = p < 0n;
  const absPaise = isNegative ? -p : p;
  const rupees = absPaise / 100n;
  const rem = absPaise % 100n;

  const rupeesFormatted = Number(rupees).toLocaleString('en-IN');
  const remStr = rem.toString().padStart(2, '0');

  return `${isNegative ? '-' : ''}₹${rupeesFormatted}.${remStr}`;
}

export function formatAmountString(amountStr?: string | null, currency: string = 'INR'): string {
  if (!amountStr || amountStr === 'NaN') return '₹0.00';
  const clean = amountStr.trim();
  if (clean === 'NaN') return '₹0.00';
  const isNeg = clean.startsWith('-');
  const raw = isNeg ? clean.substring(1) : clean;
  const parts = raw.split('.');
  const whole = parts[0] || '0';
  const fraction = (parts[1] || '00').padEnd(2, '0').slice(0, 2);

  const formattedWhole = Number(whole).toLocaleString('en-IN');
  const symbol = currency === 'INR' ? '₹' : `${currency} `;
  return `${isNeg ? '-' : ''}${symbol}${formattedWhole}.${fraction}`;
}

export function parseINRStringToPaise(amountStr: string): number {
  if (!amountStr) return 0;
  const clean = amountStr.trim().replace(/,/g, '');
  const isNeg = clean.startsWith('-');
  const raw = isNeg ? clean.substring(1) : clean;
  const parts = raw.split('.');
  const whole = BigInt(parts[0] || '0');
  const frac = BigInt((parts[1] || '00').padEnd(2, '0').slice(0, 2));
  const total = whole * 100n + frac;
  const num = Number(total);
  return isNeg ? -num : num;
}
