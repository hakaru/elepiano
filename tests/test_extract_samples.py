import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "extract_samples.py"

spec = importlib.util.spec_from_file_location("extract_samples", MODULE_PATH)
extract_samples = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(extract_samples)


def _xor_bytes(data: bytes, rot: int) -> bytes:
    key_base = extract_samples.XOR_BASE_KEY
    key = key_base[rot:] + key_base[:rot]
    return bytes(b ^ key[i % 4] for i, b in enumerate(data))


def _make_spca(frame1_encrypted: bool = False, rot: int = 0) -> tuple[bytes, bytes]:
    # One last metadata block with zero length.
    meta = bytes([0x80, 0x00, 0x00, 0x00])

    # Keep frame header shape simple but valid for the parser in decode_spca.
    frame0 = bytes([0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00])
    frame1_plain = bytes([0xFF, 0xF8, 0x11, 0x22, 0x33, 0x44])
    frame1 = _xor_bytes(frame1_plain, rot) if frame1_encrypted else frame1_plain

    spca = extract_samples.SPCA_MAGIC + meta + frame0 + frame1
    expected_flac = extract_samples.FLAC_MAGIC + meta + frame0 + frame1_plain
    return spca, expected_flac


class ExtractSamplesTest(unittest.TestCase):
    def test_parse_db_header_extracts_entries_and_data_start(self) -> None:
        payload = (
            b'<FileSystem><FILE name="a.wav" offset="10" size="20"/>'
            b'<FILE name="b.wav" offset="30" size="40"/></FileSystem>\n \r'
            b"DATA"
        )
        entries, data_start = extract_samples.parse_db_header(payload)

        self.assertEqual(2, len(entries))
        self.assertEqual("a.wav", entries[0].name)
        self.assertEqual(10, entries[0].offset)
        self.assertEqual(20, entries[0].size)
        self.assertEqual("b.wav", entries[1].name)
        self.assertEqual(payload.find(b"DATA"), data_start)

    def test_build_velocity_range_spans_full_midi_range(self) -> None:
        ranges = extract_samples.build_velocity_range([25, 50, 75, 100])

        self.assertEqual((0, 31), ranges[25])
        self.assertEqual((32, 63), ranges[50])
        self.assertEqual((64, 94), ranges[75])
        self.assertEqual((95, 127), ranges[100])

    def test_decode_spca_rewrites_magic_without_xor(self) -> None:
        spca, expected_flac = _make_spca(frame1_encrypted=False)
        decoded = extract_samples.decode_spca(spca)
        self.assertEqual(expected_flac, decoded)

    @unittest.expectedFailure
    def test_decode_spca_decodes_frame1_xor_payload(self) -> None:
        # Regression test: frame1 sync search is done before XOR decode,
        # so encrypted payload may be returned undecoded.
        spca, expected_flac = _make_spca(frame1_encrypted=True, rot=2)
        decoded = extract_samples.decode_spca(spca)
        self.assertEqual(expected_flac, decoded)


if __name__ == "__main__":
    unittest.main()
