import { Component, Input, OnDestroy, OnInit } from '@angular/core';
import { FormControl } from '@angular/forms';
import { City, Country, State } from 'country-state-city';
import { Subscription } from 'rxjs';

interface Option { name: string; isoCode: string; }

let nextUid = 0;

@Component({
  selector: 'app-location-select',
  templateUrl: './location-select.component.html',
  styleUrls: ['./location-select.component.scss']
})
export class LocationSelectComponent implements OnInit, OnDestroy {
  @Input() countryControl!: FormControl;
  @Input() stateControl!: FormControl;
  @Input() cityControl!: FormControl;

  countries: Option[] = [];
  states: Option[] = [];
  cities: { name: string }[] = [];

  legacyCountry?: string;
  legacyState?: string;
  legacyCity?: string;

  readonly uid = `loc-${++nextUid}`;

  private subs = new Subscription();

  ngOnInit(): void {
    this.countries = Country.getAllCountries()
      .map(c => ({ name: c.name, isoCode: c.isoCode }))
      .sort((a, b) => a.name.localeCompare(b.name));

    this.refreshStateList();
    this.refreshCityList();
    this.refreshLegacy();

    this.subs.add(this.countryControl.valueChanges.subscribe(() => {
      this.stateControl.setValue('', { emitEvent: false });
      this.cityControl.setValue('', { emitEvent: false });
      this.refreshStateList();
      this.refreshCityList();
      this.refreshLegacy();
    }));

    this.subs.add(this.stateControl.valueChanges.subscribe(() => {
      this.cityControl.setValue('', { emitEvent: false });
      this.refreshCityList();
      this.refreshLegacy();
    }));
  }

  ngOnDestroy(): void {
    this.subs.unsubscribe();
  }

  private refreshStateList(): void {
    const country = this.countries.find(c => c.name === this.countryControl.value);
    this.states = country
      ? State.getStatesOfCountry(country.isoCode)
          .map(s => ({ name: s.name, isoCode: s.isoCode }))
          .sort((a, b) => a.name.localeCompare(b.name))
      : [];
  }

  private refreshCityList(): void {
    const country = this.countries.find(c => c.name === this.countryControl.value);
    const state = this.states.find(s => s.name === this.stateControl.value);
    this.cities = country && state
      ? City.getCitiesOfState(country.isoCode, state.isoCode)
          .map(c => ({ name: c.name }))
          .sort((a, b) => a.name.localeCompare(b.name))
      : [];
  }

  // Surface free-text values from pre-ISO-3166 data as an extra "(unknown)"
  // option at the top of each dropdown so the operator can see what's stored
  // before overwriting it with a canonical pick.
  private refreshLegacy(): void {
    const cur = this.countryControl.value as string;
    this.legacyCountry = cur && !this.countries.find(c => c.name === cur) ? cur : undefined;

    const curState = this.stateControl.value as string;
    this.legacyState = curState && !this.states.find(s => s.name === curState) ? curState : undefined;

    const curCity = this.cityControl.value as string;
    this.legacyCity = curCity && !this.cities.find(c => c.name === curCity) ? curCity : undefined;
  }
}
