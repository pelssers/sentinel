#!/usr/bin/env python
# Interface with the SUXESs lab microcontroller
#
# 2018, Bart Pelssers

# In case of Python 2
from __future__ import print_function

import sys
import json
import requests
from collections import OrderedDict

# In case of Python 2
if sys.version_info.major == 2:
    input = raw_input  # noqa


class Interface(object):
    '''Interface class to get variables and call functions on
       the SUXESs lab microcontroller.
       Uses the Particle API: docs.particle.io
    '''
    def __init__(self, token_file='sentinel_config.json'):
        # Setup the API keys
        with open(token_file) as f:
            tokens = json.load(f)

        self.device_id = tokens['device_id']
        self.access_token = tokens['access_token']

        self.url = 'https://api.particle.io/v1/devices/%s/%s'

        # The available variables and functions
        self.exposed_variables = ['power', 'upspower', 'pressure', 'status']
        self.exposed_functions = {'alarm': ['arm', 'disarm'],
                                  'led': ['on', 'off'],
                                  'threshold': ['2400', '2500', '2600',
                                                '2700', '2800'],
                                  'test': ['']}

    def __call__(self, name, argument=None):
        '''Communicate with the microcontroller. Call with one argument
           to get variable "name", or with two arguments to call function
           "name(argument)".'''
        if argument is None:
            # Get a variable
            if name == 'status':
                # Parse the status string into a dictionary
                return self.parse_status(self.get_variable(name))
            return self.get_variable(name)
        else:
            # Call a function
            return self.call_function(name, argument)

    def get_variable(self, variable):
        '''Get 'variable', only variables in
           self.exposed_variables are allowed.
        '''
        assert variable in self.exposed_variables
        full_url = self.url % (self.device_id, variable)
        r = requests.get(full_url, params={'access_token': self.access_token})
        if 'result' not in r.json().keys():
            print("Error: no result from API, possible timeout, try again.")
            return -1
        return r.json()['result']

    def call_function(self, function, argument):
        '''Call 'function(argument)', only the specific
           combinations of function and argument in
           exposed_functions are allowed.
        '''
        assert type(argument) == str
        assert function in self.exposed_functions.keys()
        assert argument in self.exposed_functions[function]
        full_url = self.url % (self.device_id, function)
        r = requests.post(full_url, data={'access_token': self.access_token,
                                          function: argument})
        if 'return_value' not in r.json().keys():
            print("Error: no result from API, possible timeout, try again.")
            return -1
        return r.json()['return_value']

    @staticmethod
    def parse_status(status):
        '''Parse the status string into a Python dict.'''
        assert status is not -1
        d = OrderedDict()
        for pair in status.split(','):
            k, v = pair.split(':')
            if k in ['power', 'ups', 'armed']:
                d[k] = bool(int(v))
            else:
                d[k] = float(v)
        return d


class Menu(object):
    menu_options = ['See external power status.',
                    'See UPS power status.',
                    'See detector pressure.',
                    'See status.',
                    'Enable/disable the alarms.',
                    'Set pressure alarm threshold.',
                    'Exit (or Ctrl-c).']

    menu_choices = ['power', 'upspower', 'pressure',
                    'status', 'alarm', 'threshold']

    # message dict (key: tuple(message string, ok, false)
    messages = {'power': ('External power is %s.', 'OK', 'DOWN'),
                'upspower': ('UPS power is %s.', 'OK', 'DOWN'),
                'ups': ('UPS power is %s.', 'OK', 'DOWN'),
                'armed': ('Alarms are %s.', 'enabled', 'disabled'),
                'pressure': ('Pressure is %.2f mbar.', None, None),
                'pthresh': ('Pressure threshold is %.0f mbar.', None, None),
                }

    # Some colors and styles for ANSI terminals
    styles = {'bold_head': '\033[95m\033[1m',
              'head': '\033[95m',  # HEADER
              'ok': '\033[92m',  # OKGREEN
              'warn': '\033[93m',  # WARNING
              'fail': '\033[91m',  # FAIL
              'end': '\033[0m'  # ENDC
              }

    def __init__(self, interface):
        self.interface = interface

    def run(self):
        self.cprint("SUXeSs microcontroller interface.", 'bold_head')
        try:
            while True:
                print("")
                self.menu()
        except KeyboardInterrupt:
            print("\nDone")

    def string_print(self, k, v):
        message, ok, fail = self.messages[k]

        if k in ['pressure', 'pthresh']:
            self.cprint(message % v, 'ok' if v < 2501. else 'warn')
        else:  # v is bool
            m = message % ok if v else message % fail
            self.cprint(m, 'ok' if v else 'fail')

    def menu(self):
        for idx, option in enumerate(self.menu_options):
            self.cprint("%d) %s" % (idx, option), 'head')
        choice = input("Select option: ")

        if choice in ['0', '1', '2']:  # power, ups power, pressure
            var_name = self.menu_choices[int(choice)]
            print("Requesting %s values.." % var_name)
            self.string_print(var_name, self.interface(var_name))
        elif choice == '3':  # all stats
            print("Requesting values..")
            status = self.interface('status')
            for k, v in status.items():
                self.string_print(k, v)
        elif choice == '4':  # arm/disarm alarms
            self._alarm_submenu()
        elif choice == '5':  # Set pressure alarm threshold
            self._threshold_submenu()
        elif choice == '6':
            raise KeyboardInterrupt
        else:
            self.cprint('Wrong option, try again.', 'fail')

    def _alarm_submenu(self):
        self.cprint("0) Disable the alarms.", 'head')
        self.cprint("1) Enable the alarms.", 'head')

        alarm = input("Select option: ")
        if alarm == '0':
            res = self.interface('alarm', 'disarm')
            if res == 0:
                self.cprint('Alarm disabled', 'ok')
            else:
                self.cprint('Try again (wrong return value).', 'fail')
        elif alarm == '1':
            res = self.interface('alarm', 'arm')
            if res == 1:
                self.cprint('Alarm enabled.', 'ok')
            else:
                self.cprint('Try again (wrong return value).', 'fail')
        else:
            self.cprint('Wrong option: ' + alarm, 'fail')

    def _threshold_submenu(self):
        options = self.interface.exposed_functions['threshold']
        self.cprint("Set pressure alarm threshold to:", 'head')
        for idx, val in enumerate(options):
            self.cprint("%d) %s mbar" % (idx, val), 'head')

        new_threshold = input("Select option: ")
        if new_threshold in [str(i) for i in range(len(options))]:
            new_pressure = options[int(new_threshold)]
            # Attempt to set new threshold, check return value for success
            res = self.interface('threshold', new_pressure)
            message = "Pressure alarm threshold set to %d mbar." % res
            if res == int(new_pressure):
                self.cprint(message, 'ok')
            else:
                message += " Threshold NOT changed, please try again."
                self.cprint(message, 'warn')
        else:
            self.cprint("Wrong option, try again.", 'fail')

    def cprint(self, message, style):
        print(self.styles[style] + message + self.styles['end'])


if __name__ == '__main__':
    # When running as script present a simple menu to query the variables
    # enable/disable the alarms and set the pressure threshold.
    sentinel = Interface()

    menu = Menu(sentinel)
    menu.run()
