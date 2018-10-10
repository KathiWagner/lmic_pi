import sys
import argparse
import binascii
import base64
import hashlib
from Crypto import Random
from Crypto.Cipher import AES

# From: https://stackoverflow.com/questions/12524994/encrypt-decrypt-using-pycrypto-aes-256
class AESCipher(object):

    def __init__(self, key):
        self.bs = 32
        self.key = hashlib.sha256(key.encode()).digest()

    def encrypt(self, raw):
        raw = self._pad(raw)
        iv = Random.new().read(AES.block_size)
        cipher = AES.new(self.key, AES.MODE_CBC, iv)
        return base64.b64encode(iv + cipher.encrypt(raw))

    def decrypt(self, enc):
        enc = base64.b64decode(enc)
        iv = enc[:AES.block_size]
        cipher = AES.new(self.key, AES.MODE_CBC, iv)
        return self._unpad(cipher.decrypt(enc[AES.block_size:])).decode('utf-8')

    def _pad(self, s):
        return s + (self.bs - len(s) % self.bs) * chr(self.bs - len(s) % self.bs)

    @staticmethod
    def _unpad(s):
        return s[:-ord(s[len(s)-1:])]

if __name__=="__main__":
    ap = argparse.ArgumentParser(description="Encrypt a bluetooth command with a given key.")
    ap.add_argument('-k', dest='key', help='The cryptographic key')
    ap.add_argument('-t', dest='text', help='The text to encrypt')
    ap.add_argument('--silent', dest='silent', help='Do only print the result', action='store_true')
    ap.add_argument('--encrypt', help='Encrypt the given text', action="store_true")
    ap.add_argument('--decrypt', help='Decrypt the given text', action="store_true")
    args = ap.parse_args()
    cipher = AESCipher(args.key)
    if args.encrypt and not args.decrypt:
        encrypted = cipher.encrypt(args.text)
        hexout = binascii.hexlify(encrypted)
        if not args.silent:
            print('Encrypted text as hex string:\n' + hexout)
            print('Bytes: ' + str(len(hexout)))
        else:
            print(hexout)
    elif args.decrypt:
        decrypted = cipher.decrypt(binascii.unhexlify(args.text))
        if not args.silent:
            print('Decrypted text:\n' + decrypted)
        else:
            print(decrypted)
    else:
        sys.stderr.write('Error: no action given. Please either add the --encrypt or the --decrypt command.')


