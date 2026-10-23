"""Independent standard-library arithmetic reference for SPY-RISK-01 exports.

Usage: python research/verify_daily_risk_report.py <report-directory>
This verifies a supplied run; it does not acquire data or score another design.
"""
import csv
import hashlib
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path


class DailyRiskReportVerifier:
    """Independent arithmetic checks bound to one exported report directory."""

    def __init__(self, root: Path) -> None:
        self.root = root

    @staticmethod
    def _close(a: float, b: float, label: str, tolerance: float = 2e-10) -> None:
        # Relative rounding agreement for double exports; USD attribution allows 1e-7
        # absolute error on the USD 100,000 account, well below a cent.
        assert math.isclose(a, b, rel_tol=tolerance, abs_tol=tolerance), (label, a, b)

    @staticmethod
    def _cer(values: list[float]) -> float:
        return 252 * (statistics.mean(values) - 1.5 * statistics.variance(values))

    @staticmethod
    def _rows(file: Path) -> list[dict[str, str]]:
        with file.open(newline='') as handle:
            return list(csv.DictReader(handle))

    def verify(self) -> None:
        root = self.root
        summary = json.loads((root / 'summary.json').read_text())
        for artifact in summary['artifacts']:
            assert hashlib.sha256((root / artifact['file']).read_bytes()).hexdigest() == artifact['sha256']

        measurements = self._rows(root / 'measurements.csv')
        features = self._rows(root / 'features.csv')
        assert len(measurements) == 753 and len(features) == 1002
        assert len(summary['statistics']) == 120
        for m in measurements:
            self._close(float(m['daily2']), float(m['dividend_adjusted_log_return'])**2, 'daily2')
        for delay in [1, 2]:
            fs = [f for f in features if int(f['delay_minutes']) == delay]
            for i, f in enumerate(fs):
                past = measurements[232+i:252+i]
                assert f['history_first'] == past[0]['date'] and f['history_last'] == past[-1]['date']
                hd = statistics.mean(float(m['daily2']) for m in past)
                self._close(float(f['h_daily']), hd, 'lagged daily')
                complete = all(m['rv5'] for m in past)
                assert bool(f['h_intraday']) == complete
                hi = statistics.mean(float(m['rv5']) for m in past) if complete else hd
                self._close(float(f['used_h']), hi, 'used h')
                self._close(float(f['intraday_weight']), min(1, .1 / math.sqrt(252 * hi)), 'risk target')
                eligible = complete and bool(f['future_rv5'])
                assert bool(f['qlike_increment']) == eligible
                if eligible:
                    y = float(f['future_rv5'])
                    self._close(float(f['qlike_increment']), math.log(hd) + y/hd - math.log(hi) - y/hi, 'QLIKE')
                    self._close(float(f['squared_error_increment']), (y-hd)**2-(y-hi)**2, 'squared loss')
        accounts = defaultdict(list)
        for d in self._rows(root / 'daily_accounts.csv'):
            accounts[d['account'], int(d['delay_minutes']), float(d['cost_bps'])].append(d)
        assert len(accounts) == 40 and all(len(days) == 501 for days in accounts.values())
        for s in summary['statistics']:
            ds = accounts[s['account'], s['delay_minutes'], s['cost_bps']]
            if s['period'] != 'all':
                ds = [d for d in ds if d['date'].startswith(s['period'])]
            rs = [float(d['net_return']) for d in ds]
            assert len(rs) == s['sessions']
            wealth = peak = 1.
            drawdown = 0.
            for d, r in zip(ds, rs):
                wealth *= 1+r
                peak = max(peak, wealth)
                drawdown = max(drawdown, 1-wealth/peak)
                self._close(float(d['overnight_pnl_usd'])+float(d['intraday_pnl_usd']), float(d['equity_usd'])-float(d['previous_equity_usd']), 'PNL attribution', 1e-7)
                self._close(float(d['equity_usd']), float(d['liquid_wealth_usd'])+float(d['receivable_usd']), 'equity')
            self._close(s['mean_daily_return'], statistics.mean(rs), 'mean')
            self._close(s['sample_daily_standard_deviation'], statistics.stdev(rs), 'std')
            self._close(s['compounded_return'], wealth-1, 'compound')
            self._close(s['maximum_daily_close_drawdown'], drawdown, 'drawdown')
            self._close(s['cer'], self._cer(rs), 'CER')
            self._close(s['cost_usd'], sum(float(d['cost_usd']) for d in ds), 'fees')
            self._close(s['turnover_equity_units'], sum(float(d['traded_notional_usd'])/float(d['previous_equity_usd']) for d in ds), 'turnover')
            self._close(s['mean_closing_liquid_stock_weight'], statistics.mean(float(d['closing_liquid_stock_weight']) for d in ds), 'exposure')
            sorted_rs = sorted(rs)
            q = .05*(len(rs)-1); lo = math.floor(q); hi = math.ceil(q)
            self._close(s['daily_return_5th_percentile'], sorted_rs[lo]+(q-lo)*(sorted_rs[hi]-sorted_rs[lo]), 'tail quantile')
        primary = summary['primary']
        base = self._cer([float(d['net_return']) for d in accounts['intraday_risk', 1, 1.]])
        for name, key in [('daily_only', 'cer_over_daily'), ('static_risk_budget', 'cer_over_static')]:
            self._close(primary[key], base-self._cer([float(d['net_return']) for d in accounts[name, 1, 1.]]), 'CER increment')
        print('Independent report checks passed: period/case statistics, lagged features, common forecast masks and artifact hashes.')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: python research/verify_daily_risk_report.py <report-directory>')
    DailyRiskReportVerifier(Path(sys.argv[1])).verify()
