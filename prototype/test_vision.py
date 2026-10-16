import unittest

from PIL import Image, ImageDraw

from vision import detect


class VisionTests(unittest.TestCase):
    def flag(self, box, fill="red"):
        image = Image.new("RGB", (160, 120), "white")
        ImageDraw.Draw(image).rectangle(box, fill=fill)
        return image

    def test_position_and_size(self):
        left = detect(self.flag((10, 20, 45, 90)), "red")
        center = detect(self.flag((60, 20, 100, 90)), "red")
        right = detect(self.flag((115, 20, 150, 90)), "red")
        small = detect(self.flag((70, 50, 86, 72)), "red")
        self.assertLess(left["error_pct"], -20)
        self.assertLess(abs(center["error_pct"]), 10)
        self.assertGreater(right["error_pct"], 20)
        self.assertTrue(small["valid"])

    def test_wrong_color_and_largest_region(self):
        image = self.flag((12, 20, 40, 90), "blue")
        self.assertFalse(detect(image, "red")["valid"])
        ImageDraw.Draw(image).rectangle((110, 30, 150, 90), fill="red")
        self.assertGreater(detect(image, "red")["error_pct"], 20)

    def test_dim_flag(self):
        self.assertTrue(detect(self.flag((60, 20, 100, 90), (90, 5, 5)), "red")["valid"])


if __name__ == "__main__":
    unittest.main()
