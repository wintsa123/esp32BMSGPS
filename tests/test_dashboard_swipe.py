"""Exercise the actual swipe TypeScript with deterministic DOM events and frame timing."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class DashboardSwipeTest(unittest.TestCase):
    def run_case(self, name):
        result = subprocess.run(
            ["node", "tests/dashboard_swipe_check.cjs", name],
            cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_axis_lock_preserves_vertical_and_diagonal_scrolling(self):
        self.run_case("axis")

    def test_preview_progress_inert_ids_commit_and_accidental_click(self):
        self.run_case("preview")

    def test_short_cancelled_and_boundary_gestures(self):
        self.run_case("cancellation")

    def test_controls_modal_horizontal_scroll_and_multitouch_guards(self):
        self.run_case("guards")

    def test_external_tab_change_rapid_gestures_and_mouse_cancellation(self):
        self.run_case("interruptions")

    def test_reduced_motion_unmount_and_remount_cleanup(self):
        self.run_case("cleanup")


if __name__ == "__main__":
    unittest.main()
