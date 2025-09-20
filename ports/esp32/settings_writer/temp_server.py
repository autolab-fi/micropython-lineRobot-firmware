from flask import Flask, request, jsonify, send_from_directory
from flask_restful import Resource, Api
import os
import traceback
import sys
#from timeout_decorator import timeout, TimeoutError

# Request to download compiled bin file
class DownloadBinCode(Resource):
#    @timeout(180)
    def get(self):
        # read get args
        bin_name="micropython.bin"
        # path to bin file
        base_path = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))
        filepath = os.path.join(base_path, "build")
        print(filepath)
        if not os.path.exists(filepath):
            return {"msg": "file not found"}, 400
        return send_from_directory(filepath, bin_name, as_attachment=True)
        

api = Api(errors=Flask.errorhandler)

def init_routes():
    api.add_resource(DownloadBinCode, '/download_bin')


def create_app():
   app = Flask(__name__)
   app.config['SECRET_KEY'] = os.environ.get("FLASK_SECRET", default="secret-key")
   init_routes()
   api.init_app(app)
   return app

app = create_app()