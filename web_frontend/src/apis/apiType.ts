export interface Response<T> {
  code?: number;
  data?: T;
  success?: boolean;
}

export interface ResponseList<T> {
  code?: number;
  data?: T[];
  success?: boolean;
}

export interface Control {
  lamp: ControlRow[];
  speakers: ControlRow[];
  fan: ControlRow[];
  digitalTube: ControlRow[];
}

export interface ControlRow {
  key?: string;
  name?: string;
  checked?: boolean;
}

export interface ControlRequest {
  isOpen: boolean;
  which: number;
}

export interface CardProps {
  name?: string;
  count?: number;
}

export interface Environmenta {
  a9: CardProps[];
  zeebig: CardProps[];
}

