import unittest

from normalize_master_names import normalize


class NormalizeMasterNamesTests(unittest.TestCase):
    def test_only_name_reference_is_changed(self):
        self.assertEqual(normalize("さとみさんが来る", "里见先生要来",
                                   "Message", "1", "text"), "里美小姐要来")
        self.assertEqual(normalize("動画で見たことある", "在视频里见过",
                                   "Message", "2", "text"), "在视频里见过")

    def test_car_chat_typo_and_gender_are_reviewed(self):
        self.assertEqual(normalize("さとむさんの車で", "里见女士的车",
                                   "Message", "3", "text"), "里美小姐的车")
        self.assertEqual(normalize("さとみさんが来る", "里见先生来了，让他给我买",
                                   "Message", "message-event-ladder-24-1208-01",
                                   'details[key={"messageDetailId":"42-1"}].text'),
                         "里美小姐来了，让她给我买")
        self.assertEqual(normalize("さとみさんが来る", "里见先生来了，让他给我买",
                                   "Message", "message-event-ladder-24-1208-01",
                                   'details[key={"messageDetailId":"42-2"}].text'),
                         "里美小姐来了，让他给我买")


if __name__ == "__main__":
    unittest.main()
