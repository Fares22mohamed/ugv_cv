import unittest


IMAGE_WIDTH = 160


def center_x_from_error(error_pct: int) -> int:
    """Mirror the camera firmware's required error-to-pixel conversion."""
    value = round(80.0 + error_pct * 0.8)
    return max(0, min(IMAGE_WIDTH - 1, value))


def parse_protocol_line(line: str):
    """Parse one newline-terminated camera protocol message for host tests."""
    if not line.endswith("\n"):
        raise ValueError("message must end with newline")
    line = line.rstrip("\r\n")
    if line == "NONE":
        return {"valid": False}
    fields = line.split(",")
    if len(fields) != 5 or fields[0] != "FLAG":
        raise ValueError("expected FLAG,centerX,imageWidth,confidence,color")
    _, center_x, image_width, confidence, color = fields
    center_x = int(center_x)
    image_width = int(image_width)
    if not 0 <= center_x < IMAGE_WIDTH:
        raise ValueError("centerX outside image")
    if image_width != IMAGE_WIDTH:
        raise ValueError("image width must be 160")
    if confidence != "1.0":
        raise ValueError("confidence field must be the binary acceptance marker 1.0")
    if color not in {"RED", "ORANGE", "YELLOW", "GREEN", "BLUE", "PURPLE"}:
        raise ValueError("unsupported color")
    return {
        "valid": True,
        "center_x": center_x,
        "image_width": image_width,
        "confidence": confidence,
        "color": color,
    }


class CameraUartProtocolTests(unittest.TestCase):
    def test_left_center_right_mapping(self):
        self.assertLess(center_x_from_error(-50), 80)
        self.assertEqual(center_x_from_error(0), 80)
        self.assertGreater(center_x_from_error(50), 80)

    def test_center_clamps_to_image_bounds(self):
        self.assertEqual(center_x_from_error(-100), 0)
        self.assertEqual(center_x_from_error(100), 159)

    def test_valid_flag_line(self):
        parsed = parse_protocol_line("FLAG,40,160,1.0,RED\n")
        self.assertTrue(parsed["valid"])
        self.assertEqual(parsed["center_x"], 40)
        self.assertEqual(parsed["color"], "RED")

    def test_none_line(self):
        self.assertFalse(parse_protocol_line("NONE\n")["valid"])
        self.assertFalse(parse_protocol_line("NONE\r\n")["valid"])

    def test_all_supported_uppercase_colors(self):
        for color in ("RED", "ORANGE", "YELLOW", "GREEN", "BLUE", "PURPLE"):
            with self.subTest(color=color):
                self.assertEqual(
                    parse_protocol_line(f"FLAG,80,160,1.0,{color}\n")["color"],
                    color,
                )

    def test_rejects_old_v1_protocol(self):
        with self.assertRaises(ValueError):
            parse_protocol_line("V1,42,RED,1,-23,37\n")

    def test_rejects_missing_newline(self):
        with self.assertRaises(ValueError):
            parse_protocol_line("FLAG,80,160,1.0,RED")

    def test_rejects_invalid_width_and_color(self):
        with self.assertRaises(ValueError):
            parse_protocol_line("FLAG,80,320,1.0,RED\n")
        with self.assertRaises(ValueError):
            parse_protocol_line("FLAG,80,160,1.0,red\n")


if __name__ == "__main__":
    unittest.main()
