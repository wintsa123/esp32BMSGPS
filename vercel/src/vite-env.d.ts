/// <reference types="vite/client" />

type BluetoothRemoteGATTCharacteristic = {
  value?: DataView;
  writeValue?: (value: BufferSource) => Promise<void>;
  writeValueWithoutResponse?: (value: BufferSource) => Promise<void>;
  startNotifications: () => Promise<BluetoothRemoteGATTCharacteristic>;
  addEventListener: (type: "characteristicvaluechanged", listener: EventListener) => void;
};

type BluetoothRemoteGATTService = {
  getCharacteristic: (uuid: string) => Promise<BluetoothRemoteGATTCharacteristic>;
};

type BluetoothRemoteGATTServer = {
  getPrimaryService: (uuid: string) => Promise<BluetoothRemoteGATTService>;
};

type BluetoothDevice = EventTarget & {
  id: string;
  name?: string;
  gatt?: {
    connect: () => Promise<BluetoothRemoteGATTServer>;
  };
};

type BluetoothRequestDeviceOptions = {
  acceptAllDevices?: boolean;
  optionalServices?: string[];
};

interface Navigator {
  bluetooth?: {
    requestDevice: (options: BluetoothRequestDeviceOptions) => Promise<BluetoothDevice>;
  };
}

interface RequestInit {
  targetAddressSpace?: "local" | "private";
}
