# FINANCIAL BALANCE CALCULATION RULES & REVERSAL SEMANTICS

## 1. Zero Floating-Point Arithmetic
All financial balances and amounts in SettleSmart 2.0 are strictly computed and maintained as 64-bit signed integers representing **paise** (1 INR = 100 paise):
- `int64_t balance_paise`
- Ensures exact precision across billions of transactions without floating-point rounding inaccuracies.

## 2. Balance Representation
- **Creditor** (`balance > 0`): The entity has sent more money than it has received, or is net owed funds by the network.
- **Debtor** (`balance < 0`): The entity has received more money than it has sent, or net owes funds to the network.
- **Settled** (`balance == 0`): Inflowing and outflowing completed transactions are exactly equal.

## 3. Status Eligibility for Balances
To ensure accounting accuracy, only final completed operations affect an entity's balance:
- **Pending**: Ignored. Funds have not cleared.
- **Disputed**: Excluded from balance totals pending resolution.
- **Completed**: Included. Debits sender (`-amount`), credits receiver (`+amount`).
- **Reversed**: Handled via pair-wise cancellation.

## 4. Reversal Semantics & Neutralization
When a transaction $T_1$ (Sender $A \to$ Receiver $B$, amount $X$) is reversed:
1. $T_1$ is marked `Reversed`.
2. A reversal transaction $T_2$ is recorded with Sender $B \to$ Receiver $A$, amount $X$, and type `Reversal`.
3. In database recovery and in-memory calculations, both $T_1$ (`Reversed`) and $T_2$ (`Completed`) are aggregated:
   - From $T_1$: $A$ was debited $-X$, $B$ was credited $+X$.
   - From $T_2$: $B$ is debited $-X$, $A$ is credited $+X$.
   - Net effect for $A$: $-X + X = 0$.
   - Net effect for $B$: $+X - X = 0$.
4. Both entities' net balances are restored to their exact pre-transaction state without requiring physical deletion of history.

## 5. Invariant Verification
Across the entire organization, the sum of all entity balances must always equal zero:
$$\sum_{e \in \text{Entities}} \text{balance}(e) = 0$$
Any non-zero sum indicates data corruption or incomplete transaction pairs and is automatically flagged during settlement calculation.
