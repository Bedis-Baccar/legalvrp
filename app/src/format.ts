// Display helpers: times of day, durations, money, and the law's vocabulary in French.

export const DAYS = ["Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi", "Dimanche"];

export function hhmm(min: number): string {
  const m = Math.round(min);
  const h = Math.floor(m / 60);
  return `${String(h).padStart(2, "0")}:${String(m - 60 * h).padStart(2, "0")}`;
}

export function dur(min: number): string {
  const m = Math.round(min);
  if (Math.abs(m) < 60) return `${m} min`;
  const h = Math.floor(Math.abs(m) / 60);
  const r = Math.abs(m) - 60 * h;
  return `${m < 0 ? "-" : ""}${h} h${r ? ` ${String(r).padStart(2, "0")}` : ""}`;
}

const eurFmt = new Intl.NumberFormat("fr-FR", { style: "currency", currency: "EUR", maximumFractionDigits: 0 });
export const eur = (v: number) => eurFmt.format(v);
const numFmt = new Intl.NumberFormat("fr-FR", { maximumFractionDigits: 1 });
export const num = (v: number, digits = 1) =>
  new Intl.NumberFormat("fr-FR", { maximumFractionDigits: digits, minimumFractionDigits: 0 }).format(v);
export const km = (v: number) => `${numFmt.format(v)} km`;
export const pct = (v: number, digits = 0) =>
  `${v > 0 ? "+" : v < 0 ? "−" : ""}${new Intl.NumberFormat("fr-FR", { maximumFractionDigits: digits }).format(Math.abs(v) * 100)} %`;

export const SEGMENT_LABEL: Record<string, string> = {
  idle: "Au dépôt (en service)",
  prep: "Préparation au dépôt",
  drive: "Conduite",
  wait: "Attente chez le client",
  service: "Livraison",
  break: "Pause",
  close: "Clôture au dépôt",
};

export const CONTRACT_LABEL: Record<string, string> = {
  full_time: "Temps plein",
  part_time: "Temps partiel",
  temp_agency: "Intérimaire",
};

export const TYPE_LABEL: Record<string, string> = {
  grocery: "Épicerie / commerce",
  restaurant: "Restaurant",
  site: "Chantier / site",
};

export interface RuleText {
  title: string;
  text: string;
  source: string;
}

// Rule keys of src/legalvrp/domain/violation.hpp.
export const RULES: Record<string, RuleText> = {
  drive_without_break: { title: "Conduite sans pause > 4 h 30", text: "Plus de 4 h 30 de conduite sans pause qui remette le compteur à zéro.", source: "Règl. (CE) 561/2006, art. 7" },
  drive_before_break: { title: "Conduite avant la pause > 4 h 30", text: "Plus de 4 h 30 de conduite avant la première pause valable.", source: "Règl. (CE) 561/2006, art. 7" },
  drive_after_break: { title: "Conduite après la pause > 4 h 30", text: "Plus de 4 h 30 de conduite après la dernière pause valable.", source: "Règl. (CE) 561/2006, art. 7" },
  drive_between_breaks: { title: "Conduite entre deux pauses > 4 h 30", text: "Plus de 4 h 30 de conduite entre deux pauses valables.", source: "Règl. (CE) 561/2006, art. 7" },
  work_without_break: { title: "Travail sans pause > 6 h", text: "Plus de 6 h de travail consécutif (attente comprise) sans pause.", source: "Dir. 2002/15/CE art. 5 ; C. transports L3312-2" },
  work_before_break: { title: "Travail avant la pause > 6 h", text: "Plus de 6 h de travail entre la prise de poste et la première pause.", source: "Dir. 2002/15/CE art. 5 ; C. transports L3312-2" },
  work_after_break: { title: "Travail après la pause > 6 h", text: "Plus de 6 h de travail entre la dernière pause et la fin de service.", source: "Dir. 2002/15/CE art. 5 ; C. transports L3312-2" },
  work_between_breaks: { title: "Travail entre deux pauses > 6 h", text: "Plus de 6 h de travail entre deux pauses.", source: "Dir. 2002/15/CE art. 5 ; C. transports L3312-2" },
  break_too_short: { title: "Pauses trop courtes", text: "Au moins 30 min de pause au-delà de 6 h de travail, 45 min au-delà de 9 h.", source: "Dir. 2002/15/CE art. 5 ; C. transports L3312-2" },
  daily_drive_max: { title: "Conduite journalière > 9 h", text: "Plus de 9 h de conduite dans la journée.", source: "Règl. (CE) 561/2006, art. 6" },
  daily_service_max: { title: "Temps de service journalier > 12 h", text: "Plus de 12 h de temps de service dans la journée.", source: "C. transports D3312-51" },
  weekly_drive_max: { title: "Conduite hebdomadaire > 56 h", text: "Plus de 56 h de conduite dans la semaine.", source: "Règl. (CE) 561/2006, art. 6" },
  weekly_service_max: { title: "Plafond hebdomadaire du contrat", text: "Temps de service de la semaine au-delà du plafond du contrat.", source: "Contrat ; C. transports" },
  daily_rest_min: { title: "Repos journalier < 11 h", text: "Moins de 11 h de repos entre deux journées.", source: "Règl. (CE) 561/2006, art. 8" },
  time_window: { title: "Créneau client manqué", text: "La livraison commence hors du créneau d'ouverture du client.", source: "Client" },
  shift: { title: "Hors service", text: "Départ avant la prise de poste + préparation, ou retour après la fin de service autorisée (05:00 – 19:00).", source: "Contrat ; pas de travail de nuit" },
  capacity: { title: "Capacité du camion", text: "Plus de palettes que le camion n'en porte.", source: "Camion" },
  tail_lift: { title: "Hayon manquant", text: "Le client exige un hayon, le camion n'en a pas.", source: "Client" },
  consistency: { title: "Plan incohérent", text: "Les heures annoncées ne correspondent pas aux durées (trajet, livraison, pause), ou une pause est mal décrite.", source: "Vérificateur" },
  coverage: { title: "Commande oubliée", text: "Une commande n'est ni livrée ni reportée, ou l'est deux fois.", source: "Vérificateur" },
  schedule: { title: "Horaires impossibles", text: "Créneaux et horaires de service incompatibles.", source: "Vérificateur" },
};

export const ruleTitle = (k: string) => RULES[k]?.title ?? k;

export const SOLVER_LABEL: Record<string, string> = {
  ALNS: "ALNS",
  BASELINE: "Secteurs fixes",
  HAND: "Plan fait main",
  OPTIMAL: "MILP (optimal)",
  TIME_LIMIT: "MILP (limite de temps)",
};
